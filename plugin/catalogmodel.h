#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QVector>

struct AerialVariant {
    QString key;
    QUrl url;
    QString codec;
    QString dynamicRange;
    int height = 0;
};

struct AerialAsset {
    QString id;
    QString name;
    QString shotId;
    QStringList sourceLabels;
    QUrl previewUrl;
    QUrl localPreviewUrl;
    QVector<AerialVariant> variants;
};

class CatalogModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool macosEnabled READ macosEnabled WRITE setMacosEnabled NOTIFY macosEnabledChanged)
    Q_PROPERTY(bool tvosEnabled READ tvosEnabled WRITE setTvosEnabled NOTIFY tvosEnabledChanged)

public:
    enum Role {
        AssetIdRole = Qt::UserRole + 1,
        NameRole,
        PreviewUrlRole,
        PreviewAvailableRole,
        SourceLabelRole,
        CachedRole,
    };
    Q_ENUM(Role)

    explicit CatalogModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchText() const;
    void setSearchText(const QString &searchText);
    bool macosEnabled() const;
    void setMacosEnabled(bool enabled);
    bool tvosEnabled() const;
    void setTvosEnabled(bool enabled);

    void setAssets(QVector<AerialAsset> assets);
    const QVector<AerialAsset> &assets() const;
    const AerialAsset *find(const QString &id) const;
    void setPreviewUrl(const QString &id, const QUrl &localUrl);
    void notifyCacheChanged(const QString &id);

Q_SIGNALS:
    void searchTextChanged();
    void macosEnabledChanged();
    void tvosEnabledChanged();

private:
    void rebuildVisibleRows();
    int visibleRowForAsset(int assetRow) const;

    QVector<AerialAsset> m_assets;
    QVector<int> m_visibleRows;
    QString m_searchText;
    bool m_macosEnabled = true;
    bool m_tvosEnabled = true;
};
