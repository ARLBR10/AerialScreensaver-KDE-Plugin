#include "aerialbackend.h"

#include "manifestparser.h"

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KTar>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrentRun>
#ifdef AERIAL_ALLOW_INSECURE_TLS
#include <QSslError>
#endif

#include <algorithm>
#include <memory>

namespace
{
const QUrl tvosCatalogUrl(QStringLiteral("https://sylvan.apple.com/itunes-assets/Aerials126/v4/c0/45/d9/c045d9d0-9606-1535-62fe-189edb4f79eb/resources-atv-23J-2.tar"));
const QUrl macosDiscoveryUrl(QStringLiteral("https://configuration.apple.com/configurations/internetservices/aerials/resources-config-26-0.plist"));
const QString tvosSourceId(QStringLiteral("tvos-26"));
const QString macosSourceId(QStringLiteral("macos-26"));
constexpr qint64 minimumVideoBytes = 64 * 1024;
constexpr qint64 maximumDiscoveryBytes = 64 * 1024;
constexpr qint64 maximumCatalogArchiveBytes = 32 * 1024 * 1024;
constexpr qint64 maximumManifestBytes = 16 * 1024 * 1024;
constexpr qint64 maximumPreviewBytes = 8 * 1024 * 1024;
constexpr qint64 maximumVideoBytes = 2LL * 1024 * 1024 * 1024;
constexpr qint64 maximumReplyReadBytes = 256 * 1024;
constexpr int requestDeadlineMs = 5 * 60 * 1000;
constexpr int previewDeadlineMs = 60 * 1000;

QByteArray readCapped(QIODevice *device, qint64 maximumBytes)
{
    if (!device || maximumBytes < 0) {
        return {};
    }

    QByteArray data;
    while (!device->atEnd()) {
        const qint64 remaining = maximumBytes - data.size();
        if (remaining < 0) {
            return {};
        }
        const QByteArray chunk = device->read(std::min(maximumReplyReadBytes, remaining + 1));
        if (chunk.isEmpty()) {
            if (device->atEnd()) {
                break;
            }
            return {};
        }
        data.append(chunk);
        if (data.size() > maximumBytes) {
            return {};
        }
    }
    return data;
}

QByteArray readCatalogFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumManifestBytes) {
        return {};
    }
    return readCapped(&file, maximumManifestBytes);
}

QByteArray readReplyChunk(QNetworkReply *reply, qint64 remaining)
{
    if (!reply || remaining < 0) {
        return {};
    }
    return reply->read(std::min(maximumReplyReadBytes, remaining + 1));
}

void configureReply(QNetworkReply *reply, qint64 maximumBytes, int deadlineMs)
{
    if (!reply) {
        return;
    }

    reply->setReadBufferSize(std::min(maximumReplyReadBytes, maximumBytes + 1));
    const QPointer<QNetworkReply> guardedReply(reply);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [guardedReply, maximumBytes] {
        if (!guardedReply) {
            return;
        }
        bool ok = false;
        const qint64 contentLength = guardedReply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok && contentLength > maximumBytes) {
            guardedReply->abort();
        }
    });
    QTimer::singleShot(deadlineMs, reply, [guardedReply] {
        if (guardedReply && !guardedReply->isFinished()) {
            guardedReply->abort();
        }
    });
}

QByteArray manifestFromArchive(const QByteArray &data)
{
    QTemporaryFile temporary;
    if (!temporary.open() || temporary.write(data) != data.size() || !temporary.flush()) {
        return {};
    }
    KTar archive(temporary.fileName());
    if (!archive.open(QIODevice::ReadOnly)) {
        return {};
    }
    const KArchiveEntry *entry = archive.directory()->entry(QStringLiteral("entries.json"));
    if (!entry || !entry->isFile()) {
        return {};
    }
    const auto *archiveFile = static_cast<const KArchiveFile *>(entry);
    if (archiveFile->size() <= 0 || archiveFile->size() > maximumManifestBytes) {
        return {};
    }
    std::unique_ptr<QIODevice> device(archiveFile->createDevice());
    return readCapped(device.get(), maximumManifestBytes);
}

QByteArray combineManifests(const QHash<QString, QByteArray> &manifests)
{
    QJsonArray assets;
    const std::pair<QString, QString> sources[] = {
        {tvosSourceId, QStringLiteral("tvOS 26")},
        {macosSourceId, QStringLiteral("macOS 26")},
    };
    for (const auto &[sourceId, sourceLabel] : sources) {
        const auto manifest = manifests.value(sourceId);
        const auto document = QJsonDocument::fromJson(manifest);
        const auto sourceAssets = document.object().value(QStringLiteral("assets")).toArray();
        for (const auto &value : sourceAssets) {
            auto asset = value.toObject();
            asset.insert(QStringLiteral("_aerialSourceLabel"), sourceLabel);
            assets.append(asset);
        }
    }
    QJsonObject combined;
    combined.insert(QStringLiteral("version"), 1);
    combined.insert(QStringLiteral("assets"), assets);
    const QByteArray result = QJsonDocument(combined).toJson(QJsonDocument::Compact);
    return result.size() <= maximumManifestBytes ? result : QByteArray();
}

QHash<QString, QByteArray> splitLegacyManifest(const QByteArray &manifest)
{
    QJsonArray tvosAssets;
    QJsonArray macosAssets;
    const auto assets = QJsonDocument::fromJson(manifest).object().value(QStringLiteral("assets")).toArray();
    for (const auto &value : assets) {
        const auto asset = value.toObject();
        const bool hasStandardVariant = asset.contains(QStringLiteral("url-1080-H264"))
            || asset.contains(QStringLiteral("url-1080-SDR"))
            || asset.contains(QStringLiteral("url-4K-SDR"))
            || asset.contains(QStringLiteral("url-1080-HDR"))
            || asset.contains(QStringLiteral("url-4K-HDR"));
        (hasStandardVariant ? tvosAssets : macosAssets).append(asset);
    }

    QHash<QString, QByteArray> result;
    const auto addSource = [&result](const QString &sourceId, const QJsonArray &sourceAssets) {
        if (!sourceAssets.isEmpty()) {
            result.insert(sourceId, QJsonDocument(QJsonObject{{QStringLiteral("assets"), sourceAssets}}).toJson(QJsonDocument::Compact));
        }
    };
    addSource(tvosSourceId, tvosAssets);
    addSource(macosSourceId, macosAssets);
    return result;
}
}

AerialBackend::AerialBackend(QObject *parent)
    : QObject(parent)
    , m_catalog(this)
{
    loadCatalog();
}

CatalogModel *AerialBackend::catalogModel() { return &m_catalog; }
QString AerialBackend::state() const { return m_state; }
QString AerialBackend::errorMessage() const { return m_error; }
qreal AerialBackend::downloadProgress() const { return m_total > 0 ? qreal(m_received) / qreal(m_total) : 0.0; }
QString AerialBackend::downloadingAssetId() const { return m_assetId; }

bool AerialBackend::isAllowedUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && (url.host().compare(QStringLiteral("sylvan.apple.com"), Qt::CaseInsensitive) == 0
            || url.host().compare(QStringLiteral("configuration.apple.com"), Qt::CaseInsensitive) == 0);
}

QString AerialBackend::dataDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/org.kde.plasma.aerial");
}

QString AerialBackend::videoDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/org.kde.plasma.aerial/videos");
}

QString AerialBackend::previewDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/org.kde.plasma.aerial/previews");
}

QString AerialBackend::catalogPath() const { return dataDirectory() + QStringLiteral("/catalog-v2.json"); }

QString AerialBackend::sourceCatalogPath(const QString &sourceId) const
{
    return dataDirectory() + QStringLiteral("/catalog-") + sourceId + QStringLiteral(".json");
}

QString AerialBackend::cachePath(const AerialAsset &asset, const AerialVariant &variant) const
{
    const auto digest = QCryptographicHash::hash((asset.id + QLatin1Char('|') + variant.key + QLatin1Char('|') + variant.url.toString()).toUtf8(), QCryptographicHash::Sha256).toHex();
    return videoDirectory() + QLatin1Char('/') + QString::fromLatin1(digest) + QStringLiteral(".mov");
}

QString AerialBackend::previewPath(const AerialAsset &asset) const
{
    const auto digest = QCryptographicHash::hash((asset.id + QLatin1Char('|') + asset.previewUrl.toString()).toUtf8(), QCryptographicHash::Sha256).toHex();
    return previewDirectory() + QLatin1Char('/') + QString::fromLatin1(digest) + QStringLiteral(".image");
}

void AerialBackend::loadCatalog()
{
    for (const auto &sourceId : {tvosSourceId, macosSourceId}) {
        const QByteArray sourceManifest = readCatalogFile(sourceCatalogPath(sourceId));
        if (!sourceManifest.isEmpty()) {
            m_sourceManifests.insert(sourceId, sourceManifest);
        }
    }
    if (!m_sourceManifests.isEmpty() && activateCatalog(combineManifests(m_sourceManifests), false)) {
        setState(QStringLiteral("ready"));
        return;
    }
    const QByteArray legacyManifest = readCatalogFile(catalogPath());
    if (legacyManifest.isEmpty()) {
        return;
    }
    m_sourceManifests = splitLegacyManifest(legacyManifest);
    if (activateCatalog(m_sourceManifests.isEmpty() ? legacyManifest : combineManifests(m_sourceManifests), false)) {
        setState(QStringLiteral("ready"));
    }
}

bool AerialBackend::activateCatalog(const QByteArray &data, bool persist)
{
    if (data.size() > maximumManifestBytes) {
        setError(QStringLiteral("The catalog manifest is too large"));
        return false;
    }
    QString error;
    auto assets = ManifestParser::parse(data, &error);
    if (assets.isEmpty()) {
        setError(error);
        return false;
    }
    for (auto &asset : assets) {
        const QFileInfo preview(previewPath(asset));
        if (!asset.previewUrl.isEmpty() && preview.isFile() && preview.size() > 0) {
            asset.localPreviewUrl = QUrl::fromLocalFile(preview.absoluteFilePath());
        }
    }
    if (persist) {
        QDir().mkpath(dataDirectory());
        QSaveFile file(catalogPath());
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            setError(QStringLiteral("Could not save the catalog snapshot"));
            return false;
        }
    }
    m_catalog.setAssets(std::move(assets));
    Q_EMIT catalogChanged();
    return true;
}

void AerialBackend::refreshCatalog()
{
    if (m_reply || m_catalogProcessing) {
        return;
    }
    m_catalogBuffer.clear();
    m_sourceManifests.clear();
    for (const auto &sourceId : {tvosSourceId, macosSourceId}) {
        const QByteArray sourceManifest = readCatalogFile(sourceCatalogPath(sourceId));
        if (!sourceManifest.isEmpty()) {
            m_sourceManifests.insert(sourceId, sourceManifest);
        }
    }
    if (m_sourceManifests.isEmpty()) {
        const QByteArray legacyManifest = readCatalogFile(catalogPath());
        if (!legacyManifest.isEmpty()) {
            m_sourceManifests = splitLegacyManifest(legacyManifest);
        }
    }
    m_catalogErrors.clear();
    setError({});
    setState(QStringLiteral("refreshing"));
    m_refreshStage = RefreshStage::TvosCatalog;
    startRequest(tvosCatalogUrl, RequestKind::Catalog);
}

QStringList AerialBackend::assetIds() const
{
    QStringList ids;
    ids.reserve(m_catalog.assets().size());
    for (const auto &asset : m_catalog.assets()) {
        ids.append(asset.id);
    }
    return ids;
}

void AerialBackend::requestPreview(const QString &assetId)
{
    const auto *asset = m_catalog.find(assetId);
    if (!asset || asset->previewUrl.isEmpty() || !isAllowedUrl(asset->previewUrl)) {
        return;
    }
    const QFileInfo cached(previewPath(*asset));
    if (cached.isFile() && cached.size() > 0) {
        m_catalog.setPreviewUrl(assetId, QUrl::fromLocalFile(cached.absoluteFilePath()));
        return;
    }
    if (m_previewAssetId == assetId || m_pendingPreviews.contains(assetId)) {
        return;
    }
    m_pendingPreviews.push_back(assetId);
    startNextPreview();
}

void AerialBackend::startNextPreview()
{
    if (m_previewReply) {
        return;
    }
    while (!m_pendingPreviews.isEmpty()) {
        m_previewAssetId = m_pendingPreviews.takeFirst();
        const auto *asset = m_catalog.find(m_previewAssetId);
        if (!asset || asset->previewUrl.isEmpty() || !isAllowedUrl(asset->previewUrl)) {
            m_previewAssetId.clear();
            continue;
        }

        m_previewBuffer.clear();
        QNetworkRequest request(asset->previewUrl);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PlasmaAerial/0.1"));
        request.setTransferTimeout(15'000);
        m_previewReply = m_network.get(request);
        configureReply(m_previewReply, maximumPreviewBytes, previewDeadlineMs);
#ifdef AERIAL_ALLOW_INSECURE_TLS
        connect(m_previewReply, &QNetworkReply::sslErrors, this, [reply = QPointer<QNetworkReply>(m_previewReply)](const QList<QSslError> &) {
            if (reply) {
                reply->ignoreSslErrors();
            }
        });
#endif
        const QPointer<QNetworkReply> previewReply = m_previewReply;
        connect(m_previewReply, &QNetworkReply::redirected, this, [this, previewReply](const QUrl &redirect) {
            if (!previewReply) {
                return;
            }
            if (isAllowedUrl(redirect)) {
                previewReply->redirectAllowed();
            } else {
                previewReply->abort();
            }
        });
        connect(m_previewReply, &QIODevice::readyRead, this, [this] {
            if (!m_previewReply) {
                return;
            }
            const qint64 remaining = maximumPreviewBytes - m_previewBuffer.size();
            if (remaining < 0) {
                m_previewReply->abort();
                return;
            }
            const QByteArray chunk = readReplyChunk(m_previewReply, remaining);
            if (chunk.size() > remaining) {
                m_previewReply->abort();
                return;
            }
            m_previewBuffer.append(chunk);
        });
        connect(m_previewReply, &QNetworkReply::finished, this, [this] {
            const auto reply = m_previewReply;
            m_previewReply = nullptr;
            const auto *asset = m_catalog.find(m_previewAssetId);
            if (reply && asset && reply->error() == QNetworkReply::NoError && isAllowedUrl(reply->url()) && !m_previewBuffer.isEmpty()) {
                QDir().mkpath(previewDirectory());
                const QString path = previewPath(*asset);
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly) && file.write(m_previewBuffer) == m_previewBuffer.size() && file.commit()) {
                    m_catalog.setPreviewUrl(m_previewAssetId, QUrl::fromLocalFile(path));
                }
            }
            if (reply) {
                reply->deleteLater();
            }
            m_previewBuffer.clear();
            m_previewAssetId.clear();
            startNextPreview();
        });
        return;
    }
}

void AerialBackend::ensureDownloaded(const QString &assetId, const QString &qualityPolicy, int cacheLimitMiB)
{
    if (assetId.isEmpty()) {
        return;
    }
    if (m_catalogProcessing || m_reply) {
        if (m_requestKind == RequestKind::Video && m_assetId == assetId) {
            return;
        }
        const auto duplicate = std::find_if(m_pendingDownloads.cbegin(), m_pendingDownloads.cend(), [&assetId](const PendingDownload &pending) {
            return pending.assetId == assetId;
        });
        if (duplicate == m_pendingDownloads.cend()) {
            m_pendingDownloads.push_back({assetId, qualityPolicy, cacheLimitMiB});
        }
        return;
    }
    const auto *asset = m_catalog.find(assetId);
    const auto *variant = asset ? ManifestParser::selectVariant(*asset, qualityPolicy) : nullptr;
    if (!asset || !variant || !isAllowedUrl(variant->url)) {
        Q_EMIT operationFailed(assetId, QStringLiteral("No compatible media variant is available"));
        return;
    }

    m_cacheLimitBytes = std::max(256, cacheLimitMiB) * 1024LL * 1024LL;
    const QString path = cachePath(*asset, *variant);
    const QFileInfo cached(path);
    if (cached.isFile() && cached.size() >= minimumVideoBytes) {
        QFile file(path);
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
        Q_EMIT playableReady(asset->id, asset->name, QUrl::fromLocalFile(path));
        return;
    }

    QDir().mkpath(videoDirectory());
    m_downloadFile = std::make_unique<QSaveFile>(path);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        m_downloadFile.reset();
        Q_EMIT operationFailed(assetId, QStringLiteral("Could not create a cache file"));
        return;
    }
    m_assetId = asset->id;
    m_assetName = asset->name;
    m_downloadPath = path;
    m_received = 0;
    m_total = 0;
    Q_EMIT downloadingAssetIdChanged();
    Q_EMIT downloadProgressChanged();
    setState(QStringLiteral("downloading"));
    startRequest(variant->url, RequestKind::Video);
}

void AerialBackend::startRequest(const QUrl &url, RequestKind kind)
{
    if (!isAllowedUrl(url)) {
        failRequest(QStringLiteral("The remote URL is not an allowed Apple HTTPS resource"));
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PlasmaAerial/0.1"));
    request.setTransferTimeout(30'000);
    m_requestKind = kind;
    m_reply = m_network.get(request);
    const qint64 maximumResponseBytes = kind == RequestKind::Discovery
        ? maximumDiscoveryBytes
        : kind == RequestKind::Catalog ? maximumCatalogArchiveBytes : maximumVideoBytes;
    configureReply(m_reply, maximumResponseBytes, requestDeadlineMs);
#ifdef AERIAL_ALLOW_INSECURE_TLS
    connect(m_reply, &QNetworkReply::sslErrors, this, [reply = QPointer<QNetworkReply>(m_reply)](const QList<QSslError> &errors) {
        if (!reply) {
            return;
        }
        qWarning() << "Ignoring TLS certificate errors for" << reply->url() << errors;
        reply->ignoreSslErrors();
    });
#endif
    const QPointer<QNetworkReply> reply = m_reply;
    connect(m_reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &redirect) {
        if (!reply) {
            return;
        }
        if (isAllowedUrl(redirect)) {
            reply->redirectAllowed();
        } else {
            reply->abort();
        }
    });
    connect(m_reply, &QIODevice::readyRead, this, [this] {
        if (!m_reply) {
            return;
        }
        if (m_requestKind == RequestKind::Catalog || m_requestKind == RequestKind::Discovery) {
            const qint64 limit = m_requestKind == RequestKind::Discovery ? maximumDiscoveryBytes : maximumCatalogArchiveBytes;
            const qint64 remaining = limit - m_catalogBuffer.size();
            if (remaining < 0) {
                m_reply->abort();
                return;
            }
            const QByteArray chunk = readReplyChunk(m_reply, remaining);
            if (chunk.size() > remaining) {
                m_reply->abort();
                return;
            }
            m_catalogBuffer.append(chunk);
        } else if (m_downloadFile) {
            const qint64 remaining = maximumVideoBytes - m_downloadFile->size();
            if (remaining < 0) {
                m_reply->abort();
                return;
            }
            const QByteArray chunk = readReplyChunk(m_reply, remaining);
            if (chunk.size() > remaining || (!chunk.isEmpty() && m_downloadFile->write(chunk) != chunk.size())) {
                m_reply->abort();
            }
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_received = received;
        m_total = total;
        Q_EMIT downloadProgressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, &AerialBackend::requestFinished);
}

void AerialBackend::requestFinished()
{
    if (!m_reply) {
        return;
    }
    const auto reply = m_reply;
    m_reply = nullptr;
    const RequestKind kind = m_requestKind;
    m_requestKind = RequestKind::None;
    if (reply->error() != QNetworkReply::NoError || !isAllowedUrl(reply->url())) {
        const QString message = reply->error() == QNetworkReply::NoError
            ? QStringLiteral("The server redirected to an untrusted host") : reply->errorString();
        reply->deleteLater();
        if (kind == RequestKind::Catalog || kind == RequestKind::Discovery) {
            recordCatalogError(message);
            m_catalogBuffer.clear();
            advanceCatalogRefresh();
            return;
        }
        failRequest(message);
        return;
    }
    reply->deleteLater();

    if (kind == RequestKind::Discovery) {
        QString error;
        const QUrl resourceUrl = ManifestParser::parseResourcesUrl(m_catalogBuffer, &error);
        m_catalogBuffer.clear();
        if (!isAllowedUrl(resourceUrl) || resourceUrl.host().compare(QStringLiteral("sylvan.apple.com"), Qt::CaseInsensitive) != 0) {
            recordCatalogError(error.isEmpty() ? QStringLiteral("The macOS catalog URL is not an allowed Apple HTTPS resource") : error);
            advanceCatalogRefresh();
            return;
        }
        m_refreshStage = RefreshStage::MacCatalog;
        startRequest(resourceUrl, RequestKind::Catalog);
        return;
    }

    if (kind == RequestKind::Catalog) {
        catalogArchiveFinished();
        return;
    }

    if (!m_downloadFile || m_downloadFile->size() < minimumVideoBytes || !m_downloadFile->commit()) {
        failRequest(QStringLiteral("The downloaded video was incomplete or could not be activated"));
        return;
    }
    m_downloadFile.reset();
    const QString assetId = m_assetId;
    const QString assetName = m_assetName;
    const QString path = m_downloadPath;
    m_assetId.clear();
    m_assetName.clear();
    m_downloadPath.clear();
    Q_EMIT downloadingAssetIdChanged();
    setState(QStringLiteral("ready"));
    const QUrl localUrl = QUrl::fromLocalFile(path);
    markPlaying(localUrl, true);
    Q_EMIT playableReady(assetId, assetName, localUrl);
    markPlaying(localUrl, false);
    enforceCacheLimit(m_cacheLimitBytes);
    startNextPendingDownload();
}

void AerialBackend::catalogArchiveFinished()
{
    m_catalogProcessing = true;
    setState(QStringLiteral("processing"));
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher] {
        const QByteArray manifest = watcher->result();
        watcher->deleteLater();
        m_catalogProcessing = false;
        QString error;
        if (!manifest.isEmpty() && !ManifestParser::parse(manifest, &error).isEmpty()) {
            const QString sourceId = m_refreshStage == RefreshStage::TvosCatalog ? tvosSourceId : macosSourceId;
            m_sourceManifests.insert(sourceId, manifest);
            QDir().mkpath(dataDirectory());
            QSaveFile sourceFile(sourceCatalogPath(sourceId));
            if (!sourceFile.open(QIODevice::WriteOnly) || sourceFile.write(manifest) != manifest.size() || !sourceFile.commit()) {
                recordCatalogError(QStringLiteral("Could not save the %1 catalog snapshot").arg(sourceId));
            }
        } else {
            recordCatalogError(manifest.isEmpty() ? QStringLiteral("An Apple catalog archive has no usable entries.json") : error);
        }
        advanceCatalogRefresh();
    });
    watcher->setFuture(QtConcurrent::run(manifestFromArchive, std::move(m_catalogBuffer)));
    m_catalogBuffer.clear();
}

void AerialBackend::advanceCatalogRefresh()
{
    if (m_refreshStage == RefreshStage::TvosCatalog) {
        m_refreshStage = RefreshStage::MacDiscovery;
        setState(QStringLiteral("refreshing"));
        startRequest(macosDiscoveryUrl, RequestKind::Discovery);
    } else {
        finishCatalogRefresh();
    }
}

void AerialBackend::finishCatalogRefresh()
{
    m_refreshStage = RefreshStage::None;
    const QByteArray combined = combineManifests(m_sourceManifests);
    const bool activated = !m_sourceManifests.isEmpty() && activateCatalog(combined, true);
    if (activated) {
        setError(m_catalogErrors.isEmpty() ? QString() : QStringLiteral("Catalog refreshed with some unavailable sources: %1").arg(m_catalogErrors.join(QStringLiteral("; "))));
        setState(QStringLiteral("ready"));
    } else {
        if (!m_catalogErrors.isEmpty()) {
            setError(m_catalogErrors.join(QStringLiteral("; ")));
        }
        setState(m_catalog.rowCount() > 0 ? QStringLiteral("ready") : QStringLiteral("error"));
    }
    m_catalogErrors.clear();
    startNextPendingDownload();
}

void AerialBackend::recordCatalogError(const QString &message)
{
    if (!message.isEmpty()) {
        m_catalogErrors.push_back(message);
    }
}

void AerialBackend::failRequest(const QString &message)
{
    const QString failedAsset = m_assetId;
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
        m_downloadFile.reset();
    }
    m_assetId.clear();
    m_assetName.clear();
    m_downloadPath.clear();
    m_requestKind = RequestKind::None;
    Q_EMIT downloadingAssetIdChanged();
    setError(message);
    setState(m_catalog.rowCount() > 0 ? QStringLiteral("ready") : QStringLiteral("error"));
    if (!failedAsset.isEmpty()) {
        Q_EMIT operationFailed(failedAsset, message);
    }
    startNextPendingDownload();
}

void AerialBackend::cancelDownload()
{
    if (m_requestKind != RequestKind::Video || !m_reply) {
        return;
    }
    m_reply->abort();
}

void AerialBackend::markPlaying(const QUrl &localUrl, bool playing)
{
    const QString path = localUrl.isLocalFile() ? localUrl.toLocalFile() : QString();
    if (path.isEmpty()) {
        return;
    }
    if (playing) {
        m_playingPaths[path] += 1;
    } else if (m_playingPaths.contains(path)) {
        const int remaining = m_playingPaths.value(path) - 1;
        if (remaining > 0) {
            m_playingPaths[path] = remaining;
        } else {
            m_playingPaths.remove(path);
        }
    }
}

void AerialBackend::clearCache()
{
    if (m_reply && m_requestKind == RequestKind::Video) {
        cancelDownload();
    }
    const QDir directory(videoDirectory());
    for (const auto &info : directory.entryInfoList(QDir::Files)) {
        if (!m_playingPaths.contains(info.absoluteFilePath())) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

void AerialBackend::enforceCacheLimit(qint64 limitBytes)
{
    QDir directory(videoDirectory());
    auto files = directory.entryInfoList({QStringLiteral("*.mov")}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const auto &file : std::as_const(files)) {
        total += file.size();
    }
    for (const auto &file : std::as_const(files)) {
        if (total <= limitBytes) {
            break;
        }
        if (!m_playingPaths.contains(file.absoluteFilePath()) && file.absoluteFilePath() != m_downloadPath && QFile::remove(file.absoluteFilePath())) {
            total -= file.size();
        }
    }
}

void AerialBackend::startNextPendingDownload()
{
    if (m_reply || m_catalogProcessing || m_pendingDownloads.isEmpty()) {
        return;
    }
    const PendingDownload pending = m_pendingDownloads.takeFirst();
    QTimer::singleShot(0, this, [this, pending] {
        ensureDownloaded(pending.assetId, pending.qualityPolicy, pending.cacheLimitMiB);
    });
}

void AerialBackend::setState(const QString &state)
{
    if (m_state != state) {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void AerialBackend::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        Q_EMIT errorMessageChanged();
    }
}
