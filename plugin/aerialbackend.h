#pragma once

#include "catalogmodel.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QQmlEngine>
#include <QSaveFile>
#include <QHash>

class AerialBackend final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(CatalogModel *catalogModel READ catalogModel CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString downloadingAssetId READ downloadingAssetId NOTIFY downloadingAssetIdChanged)

public:
    explicit AerialBackend(QObject *parent = nullptr);
    CatalogModel *catalogModel();
    QString state() const;
    QString errorMessage() const;
    qreal downloadProgress() const;
    QString downloadingAssetId() const;

    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE QStringList assetIds() const;
    Q_INVOKABLE void requestPreview(const QString &assetId);
    Q_INVOKABLE void ensureDownloaded(const QString &assetId, const QString &qualityPolicy, int cacheLimitMiB);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void markPlaying(const QUrl &localUrl, bool playing);
    Q_INVOKABLE void clearCache();

Q_SIGNALS:
    void stateChanged();
    void errorMessageChanged();
    void downloadProgressChanged();
    void downloadingAssetIdChanged();
    void catalogChanged();
    void playableReady(const QString &assetId, const QString &name, const QUrl &localUrl);
    void operationFailed(const QString &assetId, const QString &message);

private:
    struct PendingDownload {
        QString assetId;
        QString qualityPolicy;
        int cacheLimitMiB;
    };

    enum class RequestKind { None, Discovery, Catalog, Video };
    enum class RefreshStage { None, TvosCatalog, MacDiscovery, MacCatalog };
    static bool isAllowedUrl(const QUrl &url);
    QString dataDirectory() const;
    QString videoDirectory() const;
    QString previewDirectory() const;
    QString catalogPath() const;
    QString sourceCatalogPath(const QString &sourceId) const;
    QString cachePath(const AerialAsset &asset, const AerialVariant &variant) const;
    QString previewPath(const AerialAsset &asset) const;
    void loadCatalog();
    bool activateCatalog(const QByteArray &data, bool persist);
    void catalogArchiveFinished();
    void advanceCatalogRefresh();
    void finishCatalogRefresh();
    void recordCatalogError(const QString &message);
    void startRequest(const QUrl &url, RequestKind kind);
    void requestFinished();
    void failRequest(const QString &message);
    void setState(const QString &state);
    void setError(const QString &error);
    void enforceCacheLimit(qint64 limitBytes);
    void startNextPendingDownload();
    void startNextPreview();

    CatalogModel m_catalog;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_previewReply;
    std::unique_ptr<QSaveFile> m_downloadFile;
    RequestKind m_requestKind = RequestKind::None;
    RefreshStage m_refreshStage = RefreshStage::None;
    QByteArray m_catalogBuffer;
    QByteArray m_previewBuffer;
    QHash<QString, QByteArray> m_sourceManifests;
    QStringList m_catalogErrors;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_assetId;
    QString m_assetName;
    QString m_downloadPath;
    QString m_previewAssetId;
    QHash<QString, int> m_playingPaths;
    QVector<PendingDownload> m_pendingDownloads;
    QStringList m_pendingPreviews;
    qint64 m_received = 0;
    qint64 m_total = 0;
    qint64 m_cacheLimitBytes = 4LL * 1024 * 1024 * 1024;
    bool m_catalogProcessing = false;
};
