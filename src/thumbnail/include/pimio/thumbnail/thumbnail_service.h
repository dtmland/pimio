#pragma once

#include "pimio/core/media_request.h"
#include "pimio/thumbnail/thumbnail_disk_cache.h"
#include "pimio/thumbnail/thumbnail_renderer.h"

#include <QList>
#include <QObject>

#include <memory>

namespace pimio::thumbnail {

/// Concrete MediaRequestService backed by a disk cache and a pluggable renderer.
///
/// Requests are scheduled on a private thread pool with priority determined
/// by \c MediaRequest::priority. The disk cache is checked before rendering
/// and written after every successful render, so repeated requests for the
/// same content are served without re-reading the source file.
///
/// All callbacks (ResultCallback and ErrorCallback) are invoked on the thread
/// that owns this object, never on a worker thread. If the owning thread runs
/// a Qt event loop the callbacks arrive via a queued connection after the
/// current call stack returns.
///
/// Cancellation is cooperative. cancel() sets a flag that the in-flight
/// runnable checks once before starting work. Runnables that have already
/// started are not interrupted, but their callbacks are suppressed if
/// cancel() is called before they complete.
class ThumbnailService : public QObject, public core::MediaRequestService
{
    Q_OBJECT

public:
    /// Constructs a service that uses \a cache for persistence and \a renderer
    /// for rendering. Neither pointer may be null. Both must outlive the service.
    ThumbnailService(ThumbnailDiskCache *cache, ThumbnailRenderer *renderer,
                     QObject *parent = nullptr);
    ~ThumbnailService() override;

    /// Sets the maximum number of concurrent rendering threads. Default: 2.
    ///
    /// Must be called before any requests are submitted.
    void setMaxConcurrency(int n);
    int maxConcurrency() const;

    core::MediaRequestHandle request(const core::MediaRequest &request,
                                     ResultCallback onResult,
                                     ErrorCallback onError) override;

    void cancel(core::MediaRequestHandle handle) override;
    void cancelAllExcept(const QList<core::MediaRequestHandle> &keep) override;

    /// Number of requests waiting or rendering.
    int pendingCount() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::thumbnail
