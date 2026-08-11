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

void MediaLibraryModel::reload()
{
    // Cancel all in-flight requests before resetting the item list.
    if (m_service) {
        m_service->cancelAllExcept({});
    }
    if (m_imageProvider) {
        m_imageProvider->clear();
    }

    beginResetModel();
    m_items.clear();
    m_requestIndex.clear();
    m_visibleFirst = -1;
    m_visibleLast = -1;

    if (m_db && m_db->isOpen()) {
        core::Error error;
        const QList<core::MediaId> ids = m_db->idsSorted(m_sortKey, m_sortOrder, &error);
        m_items.reserve(ids.size());
        for (const core::MediaId &id : ids) {
            Item item;
            item.id = id;
            m_items.append(std::move(item));
        }
    }

    endResetModel();
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
