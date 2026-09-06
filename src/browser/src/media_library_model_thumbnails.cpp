#include "pimio/browser/media_library_model.h"

#include "pimio/browser/thumbnail_image_provider.h"

namespace pimio::browser {

namespace {
QString requestIndexKey(const core::MediaRequest &request)
{
    return request.mediaId.value() + QLatin1Char(':') + request.cacheKey();
}
} // namespace

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
    req.absolutePath =
            m_store ? m_store->originalPath(*record, nullptr) : record->identity.absolutePath;
    if (req.absolutePath.isEmpty()) {
        item.thumbnailStatus = ThumbnailStatus::Error;
        return;
    }
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
