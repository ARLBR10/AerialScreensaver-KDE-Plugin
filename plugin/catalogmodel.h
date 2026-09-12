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
    QUrl previewUrl;
    QVector<AerialVariant> variants;
};

class CatalogModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        AssetIdRole = Qt::UserRole + 1,
        NameRole,
        PreviewUrlRole,
        CachedRole,
    };
    Q_ENUM(Role)

    explicit CatalogModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAssets(QVector<AerialAsset> assets);
    const QVector<AerialAsset> &assets() const;
    const AerialAsset *find(const QString &id) const;
    void notifyCacheChanged(const QString &id);

private:
    QVector<AerialAsset> m_assets;
};
