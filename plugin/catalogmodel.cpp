#include "catalogmodel.h"

CatalogModel::CatalogModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

QVariant CatalogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return {};
    }
    const auto &asset = m_assets.at(m_visibleRows.at(index.row()));
    switch (role) {
    case AssetIdRole:
        return asset.id;
    case NameRole:
        return asset.name;
    case PreviewUrlRole:
        return asset.localPreviewUrl;
    case PreviewAvailableRole:
        return !asset.previewUrl.isEmpty();
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
    return {{AssetIdRole, "assetId"}, {NameRole, "name"}, {PreviewUrlRole, "previewUrl"}, {PreviewAvailableRole, "previewAvailable"}, {SourceLabelRole, "sourceLabel"}, {CachedRole, "cached"}};
}

QString CatalogModel::searchText() const
{
    return m_searchText;
}

void CatalogModel::setSearchText(const QString &searchText)
{
    if (m_searchText == searchText) {
        return;
    }
    beginResetModel();
    m_searchText = searchText;
    rebuildVisibleRows();
    endResetModel();
    Q_EMIT searchTextChanged();
}

bool CatalogModel::macosEnabled() const
{
    return m_macosEnabled;
}

void CatalogModel::setMacosEnabled(bool enabled)
{
    if (m_macosEnabled == enabled) {
        return;
    }
    beginResetModel();
    m_macosEnabled = enabled;
    rebuildVisibleRows();
    endResetModel();
    Q_EMIT macosEnabledChanged();
}

bool CatalogModel::tvosEnabled() const
{
    return m_tvosEnabled;
}

void CatalogModel::setTvosEnabled(bool enabled)
{
    if (m_tvosEnabled == enabled) {
        return;
    }
    beginResetModel();
    m_tvosEnabled = enabled;
    rebuildVisibleRows();
    endResetModel();
    Q_EMIT tvosEnabledChanged();
}

void CatalogModel::setAssets(QVector<AerialAsset> assets)
{
    beginResetModel();
    m_assets = std::move(assets);
    rebuildVisibleRows();
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

void CatalogModel::setPreviewUrl(const QString &id, const QUrl &localUrl)
{
    for (int row = 0; row < m_assets.size(); ++row) {
        if (m_assets.at(row).id == id && m_assets.at(row).localPreviewUrl != localUrl) {
            m_assets[row].localPreviewUrl = localUrl;
            const int visibleRow = visibleRowForAsset(row);
            if (visibleRow >= 0) {
                const auto modelIndex = index(visibleRow);
                Q_EMIT dataChanged(modelIndex, modelIndex, {PreviewUrlRole});
            }
            return;
        }
    }
}

void CatalogModel::notifyCacheChanged(const QString &id)
{
    for (int row = 0; row < m_assets.size(); ++row) {
        if (m_assets.at(row).id == id) {
            const int visibleRow = visibleRowForAsset(row);
            if (visibleRow >= 0) {
                const auto modelIndex = index(visibleRow);
                Q_EMIT dataChanged(modelIndex, modelIndex, {CachedRole});
            }
            return;
        }
    }
}

void CatalogModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    const QString search = m_searchText.trimmed();
    for (int row = 0; row < m_assets.size(); ++row) {
        const auto &asset = m_assets.at(row);
        bool isMacos = false;
        bool isTvos = false;
        for (const auto &sourceLabel : asset.sourceLabels) {
            isMacos = isMacos || sourceLabel.startsWith(QStringLiteral("macOS"), Qt::CaseInsensitive);
            isTvos = isTvos || sourceLabel.startsWith(QStringLiteral("tvOS"), Qt::CaseInsensitive);
        }
        if ((!m_macosEnabled || !isMacos) && (!m_tvosEnabled || !isTvos)) {
            continue;
        }
        if (!search.isEmpty() && !asset.name.contains(search, Qt::CaseInsensitive) && !asset.id.contains(search, Qt::CaseInsensitive)) {
            continue;
        }
        m_visibleRows.push_back(row);
    }
}

int CatalogModel::visibleRowForAsset(int assetRow) const
{
    return m_visibleRows.indexOf(assetRow);
}
