#include "pimio/browser/media_library_model.h"

#include "pimio/browser/thumbnail_image_provider.h"

#include "pimio/core/error.h"

namespace pimio::browser {

MediaLibraryModel::MediaLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

MediaLibraryModel::~MediaLibraryModel() = default;

void MediaLibraryModel::setDatabase(projection::ProjectionDatabase *db)
{
    if (m_db == db) {
        return;
    }

    m_db = db;
    reload();
}

void MediaLibraryModel::setDurableStore(core::DurableStore *store)
{
    m_store = store;
}

void MediaLibraryModel::setRequestService(core::MediaRequestService *service)
{
    m_service = service;
}

void MediaLibraryModel::setImageProvider(ThumbnailImageProvider *provider)
{
    m_imageProvider = provider;
    if (m_imageProvider) {
        // A little slack above the model's own bound so the provider never
        // evicts an image on its own: the model is the only side that can
        // also put the row back to Pending, and an image dropped without
        // that leaves a permanently grey tile.
        syncImageProviderCapacity();
    }
}

void MediaLibraryModel::setThumbnailSize(const QSize &size)
{
    if (!size.isValid() || size.isEmpty() || size == m_thumbnailSize) {
        return;
    }
    m_thumbnailSize = size;
    invalidateThumbnails();
}

QList<int> MediaLibraryModel::thumbnailTiers()
{
    // 128 covers the smallest tile at 1x, 256 the default tile at 1x and the
    // smallest at 2x, 512 the largest tile at 2x. Nothing larger is offered
    // because nothing in the grid can use it: the detail view loads the
    // original file instead.
    return {128, 256, 512};
}

QSize MediaLibraryModel::thumbnailSizeForTile(int pixels)
{
    const QList<int> tiers = thumbnailTiers();
    int chosen = tiers.constLast();
    for (const int tier : tiers) {
        if (pixels <= tier) {
            chosen = tier;
            break;
        }
    }
    return {chosen, chosen};
}

void MediaLibraryModel::setTilePixelSize(int pixels)
{
    setThumbnailSize(thumbnailSizeForTile(pixels));
}

void MediaLibraryModel::setSortKey(projection::ProjectionDatabase::SortKey key)
{
    if (key == m_sortKey) {
        return;
    }
    m_sortKey = key;
    reload();
}

projection::ProjectionDatabase::SortKey MediaLibraryModel::sortKey() const
{
    return m_sortKey;
}

void MediaLibraryModel::setSortOrder(Qt::SortOrder order)
{
    if (order == m_sortOrder) {
        return;
    }
    m_sortOrder = order;
    reload();
}

Qt::SortOrder MediaLibraryModel::sortOrder() const
{
    return m_sortOrder;
}

void MediaLibraryModel::setSorting(int key, bool descending)
{
    using SortKey = projection::ProjectionDatabase::SortKey;
    SortKey requested = m_sortKey;
    switch (static_cast<SortKey>(key)) {
    case SortKey::CaptureTime:
    case SortKey::FileName:
    case SortKey::FileDate:
    case SortKey::FileType:
    case SortKey::FileSize:
        requested = static_cast<SortKey>(key);
        break;
    default:
        // An unknown value (a newer configuration file, or a QML typo) keeps
        // the current order rather than emptying the view.
        break;
    }

    const Qt::SortOrder order = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (requested == m_sortKey && order == m_sortOrder) {
        return;
    }
    m_sortKey = requested;
    m_sortOrder = order;
    reload();
}

QSize MediaLibraryModel::thumbnailSize() const
{
    return m_thumbnailSize;
}

void MediaLibraryModel::setPrefetchMargin(int margin)
{
    m_prefetchMargin = qMax(0, margin);
}

int MediaLibraryModel::prefetchMargin() const
{
    return m_prefetchMargin;
}

void MediaLibraryModel::setRetainedThumbnailLimit(int limit)
{
    m_retainedLimit = qMax(1, limit);
    if (m_imageProvider) {
        syncImageProviderCapacity();
    }
    while (m_retainedIds.size() > effectiveRetentionLimit()) {
        releaseThumbnail(m_retainedIds.takeLast());
    }
}

int MediaLibraryModel::retainedThumbnailLimit() const
{
    return m_retainedLimit;
}

int MediaLibraryModel::retainedThumbnailCount() const
{
    return static_cast<int>(m_retainedIds.size());
}

void MediaLibraryModel::rebuildRowIndex()
{
    m_rowById.clear();
    m_rowById.reserve(m_items.size());
    for (int row = 0; row < m_items.size(); ++row) {
        m_rowById.insert(m_items.at(row).id.value(), row);
    }
}

void MediaLibraryModel::reload()
{
    const QString visibleFirstId =
            m_visibleFirst >= 0 && m_visibleFirst < m_items.size()
            ? m_items.at(m_visibleFirst).id.value()
            : QString();
    const QString visibleLastId =
            m_visibleLast >= 0 && m_visibleLast < m_items.size()
            ? m_items.at(m_visibleLast).id.value()
            : QString();

    // Cancel all in-flight requests before rebuilding the item list: their
    // rows are about to move, so their callbacks can no longer be trusted.
    if (m_service) {
        m_service->cancelAllExcept({});
    }
    m_requestIndex.clear();

    QList<core::MediaId> ids;
    if (m_db && m_db->isOpen()) {
        core::Error error;
        ids = m_db->idsSorted(m_sortKey, m_sortOrder, &error);
    }

    const int oldCount = static_cast<int>(m_items.size());
    // Adding records to a sorted projection may place them before or between
    // rows already shown. As long as every old id remains in the same relative
    // order, this is still a pure insertion and can preserve GridView's
    // delegates and viewport instead of resetting the whole model.
    bool isInsertionOnly = ids.size() >= oldCount;
    int candidateRow = 0;
    for (int oldRow = 0; isInsertionOnly && oldRow < oldCount; ++oldRow) {
        while (candidateRow < ids.size() && ids.at(candidateRow) != m_items.at(oldRow).id) {
            ++candidateRow;
        }
        if (candidateRow == ids.size()) {
            isInsertionOnly = false;
        } else {
            ++candidateRow;
        }
    }

    if (isInsertionOnly) {
        m_items.reserve(ids.size());
        int oldRow = 0;
        int newRow = 0;
        while (newRow < ids.size()) {
            if (oldRow < m_items.size() && m_items.at(oldRow).id == ids.at(newRow)) {
                Item &item = m_items[oldRow];
                // Its request was just cancelled, so it is nobody's work now.
                if (item.thumbnailStatus == ThumbnailStatus::Loading) {
                    item.thumbnailStatus = ThumbnailStatus::Pending;
                }
                item.thumbnailHandle = {};
                item.thumbnailRequestKey.clear();
                ++oldRow;
                ++newRow;
                continue;
            }

            const int firstInserted = newRow;
            while (newRow < ids.size()
                   && !(oldRow < m_items.size()
                        && m_items.at(oldRow).id == ids.at(newRow))) {
                ++newRow;
            }
            const int insertedCount = newRow - firstInserted;
            beginInsertRows({}, firstInserted, newRow - 1);
            for (int idRow = firstInserted; idRow < newRow; ++idRow) {
                Item item;
                item.id = ids.at(idRow);
                m_items.insert(idRow, std::move(item));
            }
            endInsertRows();
            oldRow += insertedCount;
        }
        rebuildRowIndex();
    } else {
        QHash<QString, Item> previous;
        previous.reserve(oldCount);
        for (const Item &item : std::as_const(m_items)) {
            previous.insert(item.id.value(), item);
        }

        QList<Item> next;
        next.reserve(ids.size());
        for (const core::MediaId &id : std::as_const(ids)) {
            const auto it = previous.constFind(id.value());
            if (it == previous.constEnd()) {
                Item item;
                item.id = id;
                next.append(std::move(item));
                continue;
            }

            Item item = it.value();
            if (item.thumbnailStatus == ThumbnailStatus::Loading) {
                item.thumbnailStatus = ThumbnailStatus::Pending;
            }
            item.thumbnailHandle = {};
            item.thumbnailRequestKey.clear();
            next.append(std::move(item));
        }

        beginResetModel();
        m_items = std::move(next);
        rebuildRowIndex();
        endResetModel();
    }

    // Rows that are gone take their held thumbnails with them.
    for (auto it = m_retainedIds.begin(); it != m_retainedIds.end();) {
        if (m_rowById.contains(*it)) {
            ++it;
            continue;
        }
        if (m_imageProvider) {
            m_imageProvider->removeImage(*it);
        }
        it = m_retainedIds.erase(it);
    }

    if (m_items.isEmpty()) {
        m_visibleFirst = -1;
        m_visibleLast = -1;
        return;
    }

    // Re-request what the view is looking at: the reload cancelled it.
    int visibleFirst = m_visibleFirst;
    int visibleLast = m_visibleLast;
    if (isInsertionOnly && !visibleFirstId.isEmpty() && !visibleLastId.isEmpty()) {
        visibleFirst = m_rowById.value(visibleFirstId, visibleFirst);
        visibleLast = m_rowById.value(visibleLastId, visibleLast);
    }
    if (visibleFirst >= 0 && visibleLast >= visibleFirst) {
        setVisibleRange(visibleFirst, visibleLast);
    }
}

QVariantMap MediaLibraryModel::itemAt(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }

    const QModelIndex itemIndex = index(row);
    return {
        {QStringLiteral("mediaId"), data(itemIndex, MediaIdRole)},
        {QStringLiteral("absolutePath"), data(itemIndex, AbsolutePathRole)},
        {QStringLiteral("captureTimeString"), data(itemIndex, CaptureTimeStringRole)},
        {QStringLiteral("mediaKind"), data(itemIndex, MediaKindRole)},
        {QStringLiteral("thumbnailStatus"), data(itemIndex, ThumbnailStatusRole)},
    };
}

int MediaLibraryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

QVariant MediaLibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const int row = index.row();
    const Item &item = m_items.at(row);

    switch (role) {
    case MediaIdRole:
        return item.id.value();

    case AbsolutePathRole: {
        const core::MediaRecord *record = ensureRecord(row);
        if (!record) {
            return QString();
        }
        return m_store ? m_store->originalPath(*record, nullptr) : record->identity.absolutePath;
    }

    case CaptureTimeStringRole: {
        const core::MediaRecord *record = ensureRecord(row);
        if (!record) {
            return QString();
        }
        const core::CaptureTime &ct = record->metadata.captureTime;
        if (!ct.isValid()) {
            return QString();
        }
        return ct.wallClock().toString(Qt::ISODate);
    }

    case MediaKindRole: {
        const core::MediaRecord *record = ensureRecord(row);
        return record ? static_cast<int>(record->metadata.kind)
                      : static_cast<int>(core::MediaKind::Unknown);
    }

    case ThumbnailStatusRole:
        return static_cast<int>(item.thumbnailStatus);

    case ThumbnailImageRole:
        return item.thumbnailImage;

    default:
        return {};
    }
}

QHash<int, QByteArray> MediaLibraryModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names.insert(MediaIdRole, "mediaId");
    names.insert(AbsolutePathRole, "absolutePath");
    names.insert(CaptureTimeStringRole, "captureTimeString");
    names.insert(MediaKindRole, "mediaKind");
    names.insert(ThumbnailStatusRole, "thumbnailStatus");
    names.insert(ThumbnailImageRole, "thumbnailImage");
    return names;
}

const core::MediaRecord *MediaLibraryModel::ensureRecord(int row) const
{
    Item &item = const_cast<Item &>(m_items.at(row));
    if (item.record.has_value()) {
        return &*item.record;
    }
    if (!m_db || !m_db->isOpen()) {
        return nullptr;
    }
    core::Error error;
    item.record = m_db->load(item.id, &error);
    return item.record.has_value() ? &*item.record : nullptr;
}

} // namespace pimio::browser
