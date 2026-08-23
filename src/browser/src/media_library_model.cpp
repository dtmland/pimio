#include "pimio/browser/media_library_model.h"

#include "pimio/browser/thumbnail_image_provider.h"

#include "pimio/core/error.h"

namespace pimio::browser {

namespace {
QString requestIndexKey(const core::MediaRequest &request)
{
    return request.mediaId.value() + QLatin1Char(':') + request.cacheKey();
}
} // namespace

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

void MediaLibraryModel::invalidateThumbnails()
{
    if (m_items.isEmpty()) {
        return;
    }
    if (m_service) {
        m_service->cancelAllExcept({});
    }
    if (m_imageProvider) {
        m_imageProvider->clear();
    }
    m_requestIndex.clear();
    m_retainedIds.clear();
    for (Item &item : m_items) {
        item.thumbnailStatus = ThumbnailStatus::Pending;
        item.thumbnailImage = QImage();
        item.thumbnailHandle = {};
        item.thumbnailRequestKey.clear();
    }
    emit dataChanged(index(0), index(m_items.size() - 1),
                     {ThumbnailStatusRole, ThumbnailImageRole});

    // Re-request what the view is currently showing. Without this the grid
    // would stay on placeholders until the next scroll.
    if (m_visibleFirst >= 0 && m_visibleLast >= m_visibleFirst) {
        setVisibleRange(m_visibleFirst, m_visibleLast);
    }
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

int MediaLibraryModel::effectiveRetentionLimit() const
{
    if (m_visibleFirst < 0 || m_visibleLast < m_visibleFirst) {
        return m_retainedLimit;
    }
    // Everything the current window can ask for, plus a little slack. A bound
    // smaller than the window would drop a thumbnail the grid is showing and
    // immediately ask for it again.
    const int window = m_visibleLast - m_visibleFirst + 1 + 2 * m_prefetchMargin + 16;
    return qMax(m_retainedLimit, window);
}

void MediaLibraryModel::syncImageProviderCapacity()
{
    if (m_imageProvider) {
        m_imageProvider->setCapacity(effectiveRetentionLimit() + 8);
    }
}

void MediaLibraryModel::retainThumbnail(int row)
{
    syncImageProviderCapacity();

    if (row < 0 || row >= m_items.size()) {
        return;
    }
    const QString id = m_items.at(row).id.value();
    m_retainedIds.removeOne(id);
    m_retainedIds.prepend(id);

    const int limit = effectiveRetentionLimit();
    const int fetchFirst = m_visibleFirst < 0 ? -1 : qMax(0, m_visibleFirst - m_prefetchMargin);
    const int fetchLast = m_visibleLast < 0
            ? -1
            : qMin(static_cast<int>(m_items.size()) - 1, m_visibleLast + m_prefetchMargin);
    while (m_retainedIds.size() > limit) {
        const QString evicted = m_retainedIds.takeLast();
        const auto it = m_rowById.constFind(evicted);
        const int evictedRow = it == m_rowById.constEnd() ? -1 : it.value();
        releaseThumbnail(evicted);
        // Dropping a row the window still covers would leave it showing a
        // placeholder until the next scroll, so it is requested again now.
        if (evictedRow >= fetchFirst && evictedRow <= fetchLast && evictedRow >= 0) {
            requestThumbnailIfNeeded(evictedRow);
        }
    }
}

void MediaLibraryModel::releaseThumbnail(const QString &mediaId)
{
    if (m_imageProvider) {
        m_imageProvider->removeImage(mediaId);
    }
    const auto it = m_rowById.constFind(mediaId);
    if (it == m_rowById.constEnd()) {
        return;
    }
    const int row = it.value();
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    Item &item = m_items[row];
    item.thumbnailImage = QImage();
    if (item.thumbnailStatus == ThumbnailStatus::Ready) {
        item.thumbnailStatus = ThumbnailStatus::Pending;
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {ThumbnailStatusRole, ThumbnailImageRole});
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
            Q_ASSERT(oldRow == firstInserted);
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

void MediaLibraryModel::setVisibleRange(int first, int last)
{
    const int count = m_items.size();
    if (count == 0) {
        return;
    }

    // Clamp visible range.
    const int visFirst = qBound(0, first, count - 1);
    const int visLast = qBound(0, last, count - 1);
    m_visibleFirst = visFirst;
    m_visibleLast = visLast;
    syncImageProviderCapacity();

    // Expand to include the prefetch margin.
    const int fetchFirst = qMax(0, visFirst - m_prefetchMargin);
    const int fetchLast = qMin(count - 1, visLast + m_prefetchMargin);

    // Collect handles that should remain alive.
    QList<core::MediaRequestHandle> keep;
    for (int row = fetchFirst; row <= fetchLast; ++row) {
        const Item &item = m_items.at(row);
        if (item.thumbnailStatus == ThumbnailStatus::Loading && item.thumbnailHandle.isValid()) {
            keep.append(item.thumbnailHandle);
        }
    }

    // Cancel requests outside the window.
    if (m_service) {
        m_service->cancelAllExcept(keep);
    }

    // Reset status for items that were loading but got cancelled.
    for (int row = 0; row < count; ++row) {
        if (row >= fetchFirst && row <= fetchLast) {
            continue;
        }
        Item &item = m_items[row];
        if (item.thumbnailStatus == ThumbnailStatus::Loading) {
            item.thumbnailStatus = ThumbnailStatus::Pending;
            item.thumbnailHandle = {};
            m_requestIndex.remove(item.thumbnailRequestKey);
            item.thumbnailRequestKey.clear();
        }
    }

    // Request thumbnails for the new window.
    for (int row = fetchFirst; row <= fetchLast; ++row) {
        requestThumbnailIfNeeded(row);
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

void MediaLibraryModel::requestThumbnail(int row)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    requestThumbnailIfNeeded(row);
}

void MediaLibraryModel::refreshThumbnail(int row)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    Item &item = m_items[row];
    if (item.thumbnailStatus == ThumbnailStatus::Loading) {
        return;
    }
    const QString id = item.id.value();
    m_retainedIds.removeOne(id);
    releaseThumbnail(id);
    item.thumbnailStatus = ThumbnailStatus::Pending;
    requestThumbnailIfNeeded(row);
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
        return record ? record->identity.absolutePath : QString();
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

void MediaLibraryModel::requestThumbnailIfNeeded(int row) const
{
    Item &item = const_cast<Item &>(m_items.at(row));
    if (item.thumbnailStatus != ThumbnailStatus::Pending) {
        return;
    }
    if (!m_service) {
        return;
    }
    const core::MediaRecord *record = ensureRecord(row);
    if (!record) {
        return;
    }

    core::MediaRequest req;
    req.mediaId = item.id;
    req.fingerprint = record->fingerprint;
    req.absolutePath = record->identity.absolutePath;
    req.kind = core::MediaRequestKind::Thumbnail;
    req.targetSize = m_thumbnailSize;
    req.priority = core::JobPriority::Interactive;

    item.thumbnailStatus = ThumbnailStatus::Loading;
    item.thumbnailRequestKey = requestIndexKey(req);
    item.thumbnailHandle = m_service->request(
            req,
            [this](const core::MediaRequest &r, const core::MediaResult &res) {
                const_cast<MediaLibraryModel *>(this)->onThumbnailResult(r, res);
            },
            [this](const core::MediaRequest &r, const core::Error &err) {
                const_cast<MediaLibraryModel *>(this)->onThumbnailError(r, err);
            });
    m_requestIndex.insert(item.thumbnailRequestKey, row);
}

void MediaLibraryModel::onThumbnailResult(const core::MediaRequest &request,
                                           const core::MediaResult &result)
{
    const auto it = m_requestIndex.find(requestIndexKey(request));
    if (it == m_requestIndex.end()) {
        return;
    }
    const int row = it.value();
    m_requestIndex.erase(it);

    if (row < 0 || row >= m_items.size()) {
        return;
    }

    Item &item = m_items[row];
    item.thumbnailHandle = {};
    item.thumbnailRequestKey.clear();
    item.thumbnailImage = QImage::fromData(result.bytes);

    if (item.thumbnailImage.isNull()) {
        item.thumbnailStatus = ThumbnailStatus::Error;
    } else {
        item.thumbnailStatus = ThumbnailStatus::Ready;
    }

    if (m_imageProvider && item.thumbnailStatus == ThumbnailStatus::Ready) {
        m_imageProvider->setImage(item.id.value(), item.thumbnailImage);
    }

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {ThumbnailStatusRole, ThumbnailImageRole});

    // After the row is reported Ready, so that a thumbnail dropped to make
    // room for this one is reported in the order it happened.
    if (item.thumbnailStatus == ThumbnailStatus::Ready) {
        retainThumbnail(row);
    }
}

void MediaLibraryModel::onThumbnailError(const core::MediaRequest &request,
                                          const core::Error &error)
{
    // Cancelled requests are not errors from the model's perspective; they
    // should be re-requested when the item scrolls back into view.
    if (error.code() == core::ErrorCode::Cancelled) {
        return;
    }

    const auto it = m_requestIndex.find(requestIndexKey(request));
    if (it == m_requestIndex.end()) {
        return;
    }
    const int row = it.value();
    m_requestIndex.erase(it);

    if (row < 0 || row >= m_items.size()) {
        return;
    }

    Item &item = m_items[row];
    item.thumbnailStatus = ThumbnailStatus::Error;
    item.thumbnailHandle = {};
    item.thumbnailRequestKey.clear();

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {ThumbnailStatusRole});
}

} // namespace pimio::browser
