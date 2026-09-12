#include "catalogmodel.h"

CatalogModel::CatalogModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_assets.size();
}

QVariant CatalogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_assets.size()) {
        return {};
    }
    const auto &asset = m_assets.at(index.row());
    switch (role) {
    case AssetIdRole:
        return asset.id;
    case NameRole:
        return asset.name;
    case PreviewUrlRole:
        return asset.previewUrl;
    case SourceLabelRole:
        if (asset.sourceLabels.contains(QStringLiteral("macOS 26")) && asset.sourceLabels.contains(QStringLiteral("tvOS 26"))) {
            return QStringLiteral("macOS and tvOS 26");
        }
        return asset.sourceLabels.join(QStringLiteral(" and "));
    case CachedRole:
        return false;
    default:
        return {};
    }
}

QHash<int, QByteArray> CatalogModel::roleNames() const
{
    return {{AssetIdRole, "assetId"}, {NameRole, "name"}, {PreviewUrlRole, "previewUrl"}, {SourceLabelRole, "sourceLabel"}, {CachedRole, "cached"}};
}

void CatalogModel::setAssets(QVector<AerialAsset> assets)
{
    beginResetModel();
    m_assets = std::move(assets);
    endResetModel();
}

const QVector<AerialAsset> &CatalogModel::assets() const
{
    return m_assets;
}

const AerialAsset *CatalogModel::find(const QString &id) const
{
    for (const auto &asset : m_assets) {
        if (asset.id == id) {
            return &asset;
        }
    }
    return nullptr;
}

void CatalogModel::notifyCacheChanged(const QString &id)
{
    for (int row = 0; row < m_assets.size(); ++row) {
        if (m_assets.at(row).id == id) {
            const auto modelIndex = index(row);
            Q_EMIT dataChanged(modelIndex, modelIndex, {CachedRole});
            return;
        }
    }
}
