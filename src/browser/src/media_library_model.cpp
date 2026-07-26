#include "pimio/browser/media_library_model.h"

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

void MediaLibraryModel::setRequestService(core::MediaRequestService *service)
{
    m_service = service;
}

void MediaLibraryModel::setThumbnailSize(const QSize &size)
{
    m_thumbnailSize = size;
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

    beginResetModel();
    m_items.clear();
    m_requestIndex.clear();

    if (m_db && m_db->isOpen()) {
        core::Error error;
        const QList<core::MediaId> ids = m_db->idsByCaptureTime(&error);
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
            m_requestIndex.remove(item.id.value());
        }
    }

    // Request thumbnails for the new window.
    for (int row = fetchFirst; row <= fetchLast; ++row) {
        requestThumbnailIfNeeded(row);
    }
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
    item.thumbnailHandle = m_service->request(
            req,
            [this](const core::MediaRequest &r, const core::MediaResult &res) {
                const_cast<MediaLibraryModel *>(this)->onThumbnailResult(r, res);
            },
            [this](const core::MediaRequest &r, const core::Error &err) {
                const_cast<MediaLibraryModel *>(this)->onThumbnailError(r, err);
            });
    m_requestIndex.insert(req.cacheKey(), row);
}

void MediaLibraryModel::onThumbnailResult(const core::MediaRequest &request,
                                           const core::MediaResult &result)
{
    const auto it = m_requestIndex.find(request.cacheKey());
    if (it == m_requestIndex.end()) {
        return;
    }
    const int row = it.value();
    m_requestIndex.erase(it);

    if (row < 0 || row >= m_items.size()) {
        return;
    }

    Item &item = m_items[row];
    item.thumbnailStatus = ThumbnailStatus::Ready;
    item.thumbnailHandle = {};
    item.thumbnailImage = QImage::fromData(result.bytes);

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

    const auto it = m_requestIndex.find(request.cacheKey());
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

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {ThumbnailStatusRole});
}

} // namespace pimio::browser
