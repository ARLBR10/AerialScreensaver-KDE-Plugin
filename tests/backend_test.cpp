#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "manifestparser.h"

#define private public
#include "aerialbackend.h"
#undef private

namespace
{
void useTemporaryPaths(const QTemporaryDir &directory)
{
    qputenv("XDG_DATA_HOME", directory.path().toUtf8());
    qputenv("XDG_CACHE_HOME", directory.path().toUtf8());
}

bool writeCatalog(const QByteArray &data)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/org.kde.plasma.aerial");
    if (!QDir().mkpath(directory)) {
        return false;
    }
    QFile file(directory + QStringLiteral("/catalog-v2.json"));
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray cachedManifest()
{
    return QByteArrayLiteral("{\"assets\":[{\"id\":\"cached\",\"accessibilityLabel\":\"Cached asset\",\"url-1080-H264\":\"https://sylvan.apple.com/videos/cached.mov\"}]}");
}

bool createCachedVideo(AerialBackend &backend, const QString &assetId)
{
    const auto *asset = backend.catalogModel()->find(assetId);
    if (!asset) {
        return false;
    }
    const auto *variant = ManifestParser::selectVariant(*asset, QStringLiteral("compatibility"));
    if (!variant) {
        return false;
    }
    const QString path = backend.cachePath(*asset, *variant);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(64 * 1024, 'x')) == 64 * 1024;
}
}

class BackendTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void usesCachedVideoWithoutNetwork();
    void queuesDownloadWhileCatalogProcesses();
    void rejectsMissingAssetWithoutNetwork();
};

void BackendTest::usesCachedVideoWithoutNetwork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    useTemporaryPaths(directory);
    QVERIFY(writeCatalog(cachedManifest()));

    AerialBackend backend;
    QVERIFY(createCachedVideo(backend, QStringLiteral("cached")));

    QSignalSpy playableSpy(&backend, &AerialBackend::playableReady);
    QSignalSpy failureSpy(&backend, &AerialBackend::operationFailed);
    backend.ensureDownloaded(QStringLiteral("cached"), QStringLiteral("compatibility"), 256);

    QCOMPARE(playableSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 0);
    QCOMPARE(backend.state(), QStringLiteral("ready"));
    const auto arguments = playableSpy.constFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("cached"));
    QVERIFY(arguments.at(2).toUrl().isLocalFile());
}

void BackendTest::queuesDownloadWhileCatalogProcesses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    useTemporaryPaths(directory);
    QVERIFY(writeCatalog(cachedManifest()));

    AerialBackend backend;
    QVERIFY(createCachedVideo(backend, QStringLiteral("cached")));
    QSignalSpy playableSpy(&backend, &AerialBackend::playableReady);

    backend.m_catalogProcessing = true;
    backend.ensureDownloaded(QStringLiteral("cached"), QStringLiteral("compatibility"), 256);
    QCOMPARE(backend.m_pendingDownloads.size(), 1);
    QCOMPARE(playableSpy.count(), 0);

    backend.startNextPendingDownload();
    QCoreApplication::processEvents();
    QCOMPARE(backend.m_pendingDownloads.size(), 1);
    QCOMPARE(playableSpy.count(), 0);

    backend.m_catalogProcessing = false;
    backend.startNextPendingDownload();
    QTRY_COMPARE(playableSpy.count(), 1);
    QCOMPARE(backend.m_pendingDownloads.size(), 0);
}

void BackendTest::rejectsMissingAssetWithoutNetwork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    useTemporaryPaths(directory);

    AerialBackend backend;
    QSignalSpy failureSpy(&backend, &AerialBackend::operationFailed);
    backend.ensureDownloaded(QStringLiteral("missing"), QStringLiteral("compatibility"), 256);

    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(backend.m_reply, nullptr);
    QVERIFY(!failureSpy.constFirst().at(1).toString().isEmpty());
}

QTEST_MAIN(BackendTest)
#include "backend_test.moc"
