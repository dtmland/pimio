#include "app_test_support.h"

#include <QVariant>

#include <utility>

namespace pimio::tests::app_support {

SyntheticMediaModel::SyntheticMediaModel(int count, int thumbnailStatus, QString absolutePath,
                                         QObject *parent)
    : QAbstractListModel(parent)
    , m_thumbnailStatus(thumbnailStatus)
    , m_absolutePath(std::move(absolutePath))
{
    m_ids.reserve(count);
    for (int row = 0; row < count; ++row) {
        m_ids.append(QStringLiteral("item-%1").arg(row));
    }
}

int SyntheticMediaModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_ids.size();
}

QVariant SyntheticMediaModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size()) {
        return {};
    }
    switch (role) {
    case MediaIdRole:
        return m_ids.at(index.row());
    case AbsolutePathRole:
        return m_absolutePath.isEmpty() ? QStringLiteral("/library/%1.jpg").arg(m_ids.at(index.row()))
                                        : m_absolutePath;
    case CaptureTimeStringRole:
        return QStringLiteral("2026-01-01T00:00:00");
    case MediaKindRole:
        return 1;
    case ThumbnailStatusRole:
        return m_thumbnailStatus;
    default:
        return {};
    }
}

QHash<int, QByteArray> SyntheticMediaModel::roleNames() const
{
    return {
        {MediaIdRole, "mediaId"},
        {AbsolutePathRole, "absolutePath"},
        {CaptureTimeStringRole, "captureTimeString"},
        {MediaKindRole, "mediaKind"},
        {ThumbnailStatusRole, "thumbnailStatus"},
        {ThumbnailImageRole, "thumbnailImage"},
    };
}

void SyntheticMediaModel::setVisibleRange(int first, int last)
{
    emit visibleRangeChanged(first, last);
}

void SyntheticMediaModel::requestThumbnail(int)
{
}

void SyntheticMediaModel::refreshThumbnail(int row)
{
    m_refreshedRows.append(row);
}

QList<int> SyntheticMediaModel::refreshedRows() const
{
    return m_refreshedRows;
}

void SyntheticMediaModel::prependRows(int count)
{
    if (count <= 0) {
        return;
    }
    beginInsertRows({}, 0, count - 1);
    for (int row = 0; row < count; ++row) {
        m_ids.insert(row, QStringLiteral("prepended-%1").arg(m_nextPrependedId++));
    }
    endInsertRows();
}

void SyntheticMediaModel::removeLeadingRows(int count)
{
    const int removed = qBound(0, count, static_cast<int>(m_ids.size()));
    if (removed == 0) {
        return;
    }
    beginRemoveRows({}, 0, removed - 1);
    m_ids.remove(0, removed);
    endRemoveRows();
}

QVariantMap SyntheticMediaModel::itemAt(int row) const
{
    const QModelIndex itemIndex = index(row);
    return {
        {QStringLiteral("mediaId"), data(itemIndex, MediaIdRole)},
        {QStringLiteral("absolutePath"), data(itemIndex, AbsolutePathRole)},
        {QStringLiteral("captureTimeString"), data(itemIndex, CaptureTimeStringRole)},
        {QStringLiteral("mediaKind"), data(itemIndex, MediaKindRole)},
        {QStringLiteral("thumbnailStatus"), data(itemIndex, ThumbnailStatusRole)},
    };
}

QQuickItem *findVisualItem(QQuickItem *root, const QString &objectName)
{
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *match = findVisualItem(child, objectName)) {
            return match;
        }
    }
    return nullptr;
}

} // namespace pimio::tests::app_support
