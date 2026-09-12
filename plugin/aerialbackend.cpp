#include "aerialbackend.h"

#include "manifestparser.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#ifdef AERIAL_ALLOW_INSECURE_TLS
#include <QSslError>
#endif

#include <algorithm>

namespace
{
const QUrl catalogUrl(QStringLiteral("https://sylvan.apple.com/Aerials/2x/entries.json"));
constexpr qint64 minimumVideoBytes = 64 * 1024;
constexpr qint64 maximumCatalogBytes = 32 * 1024 * 1024;
constexpr qint64 maximumVideoBytes = 2LL * 1024 * 1024 * 1024;
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
        && url.host().compare(QStringLiteral("sylvan.apple.com"), Qt::CaseInsensitive) == 0;
}

QString AerialBackend::dataDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/org.kde.plasma.aerial");
}

QString AerialBackend::videoDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/org.kde.plasma.aerial/videos");
}

QString AerialBackend::catalogPath() const { return dataDirectory() + QStringLiteral("/catalog.json"); }

QString AerialBackend::cachePath(const AerialAsset &asset, const AerialVariant &variant) const
{
    const auto digest = QCryptographicHash::hash((asset.id + QLatin1Char('|') + variant.key + QLatin1Char('|') + variant.url.toString()).toUtf8(), QCryptographicHash::Sha256).toHex();
    return videoDirectory() + QLatin1Char('/') + QString::fromLatin1(digest) + QStringLiteral(".mov");
}

void AerialBackend::loadCatalog()
{
    QFile file(catalogPath());
    if (file.open(QIODevice::ReadOnly) && activateCatalog(file.readAll(), false)) {
        setState(QStringLiteral("ready"));
    }
}

bool AerialBackend::activateCatalog(const QByteArray &data, bool persist)
{
    QString error;
    auto assets = ManifestParser::parse(data, &error);
    if (assets.isEmpty()) {
        setError(error);
        return false;
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
    if (m_reply) {
        return;
    }
    m_catalogBuffer.clear();
    setError({});
    setState(QStringLiteral("refreshing"));
    startRequest(catalogUrl, RequestKind::Catalog);
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

void AerialBackend::ensureDownloaded(const QString &assetId, const QString &qualityPolicy, int cacheLimitMiB)
{
    if (assetId.isEmpty()) {
        return;
    }
    if (m_reply) {
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
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PlasmaAerial/0.1"));
    request.setTransferTimeout(30'000);
    m_requestKind = kind;
    m_reply = m_network.get(request);
#ifdef AERIAL_ALLOW_INSECURE_TLS
    connect(m_reply, &QNetworkReply::sslErrors, this, [reply = QPointer<QNetworkReply>(m_reply)](const QList<QSslError> &errors) {
        if (!reply) {
            return;
        }
        qWarning() << "Ignoring TLS certificate errors for" << reply->url() << errors;
        reply->ignoreSslErrors();
    });
#endif
    connect(m_reply, &QNetworkReply::redirected, this, [this](const QUrl &redirect) {
        if (!isAllowedUrl(redirect) && m_reply) {
            m_reply->abort();
        }
    });
    connect(m_reply, &QIODevice::readyRead, this, [this] {
        if (!m_reply) {
            return;
        }
        const QByteArray chunk = m_reply->readAll();
        if (m_requestKind == RequestKind::Catalog) {
            m_catalogBuffer.append(chunk);
            if (m_catalogBuffer.size() > maximumCatalogBytes) {
                m_reply->abort();
            }
        } else if (m_downloadFile) {
            if (m_downloadFile->size() + chunk.size() > maximumVideoBytes || m_downloadFile->write(chunk) != chunk.size()) {
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
        failRequest(message);
        return;
    }
    reply->deleteLater();

    if (kind == RequestKind::Catalog) {
        if (activateCatalog(m_catalogBuffer, true)) {
            setError({});
            setState(QStringLiteral("ready"));
        } else {
            setState(m_catalog.rowCount() > 0 ? QStringLiteral("ready") : QStringLiteral("error"));
        }
        m_catalogBuffer.clear();
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
    enforceCacheLimit(m_cacheLimitBytes);
    setState(QStringLiteral("ready"));
    Q_EMIT playableReady(assetId, assetName, QUrl::fromLocalFile(path));
    startNextPendingDownload();
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
    if (m_reply || m_pendingDownloads.isEmpty()) {
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
