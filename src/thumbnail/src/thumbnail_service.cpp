#include "pimio/thumbnail/thumbnail_service.h"

#include <QHash>
#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

namespace pimio::thumbnail {

namespace {

// Convert the library's JobPriority to a QThreadPool priority integer.
// QThreadPool runs higher integers first.
int poolPriority(core::JobPriority priority)
{
    switch (priority) {
    case core::JobPriority::Interactive:
        return 8;
    case core::JobPriority::UserInitiated:
        return 6;
    case core::JobPriority::Background:
        return 4;
    case core::JobPriority::Opportunistic:
        return 2;
    }
    return 4;
}

} // namespace

// Per-request state shared between the service (main thread) and the runnable
// (pool thread). The service owns the shared_ptr; the runnable holds a weak
// reference so it can tell when the service cancelled the request.
struct PendingItem
{
    core::MediaRequest request;
    core::MediaRequestService::ResultCallback onResult;
    core::MediaRequestService::ErrorCallback onError;
    std::function<void()> onFinished;
    std::atomic<bool> cancelled{false};
};

class ThumbnailRunnable : public QRunnable
{
public:
    ThumbnailRunnable(std::shared_ptr<PendingItem> item, quint64 handle,
                      ThumbnailDiskCache *cache, ThumbnailRenderer *renderer, QObject *service)
        : m_item(std::move(item))
        , m_handle(handle)
        , m_cache(cache)
        , m_renderer(renderer)
        , m_service(service)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_item->cancelled.load()) {
            return;
        }

        // Cache hit: deliver without rendering.
        auto cached = m_cache->load(m_item->request.cacheKey());
        if (cached) {
            if (!m_item->cancelled.load()) {
                core::MediaResult result;
                result.bytes = *cached;
                result.format = QStringLiteral("jpeg");
                deliverResult(result);
            }
            return;
        }

        if (m_item->cancelled.load()) {
            return;
        }

        // Cache miss: render.
        core::Error error;
        const core::MediaResult result = m_renderer->render(m_item->request, &error);

        if (m_item->cancelled.load()) {
            return;
        }

        if (error.isError()) {
            deliverError(error);
            return;
        }

        m_cache->store(m_item->request.cacheKey(), result.bytes);

        if (!m_item->cancelled.load()) {
            deliverResult(result);
        }
    }

private:
    void deliverResult(const core::MediaResult &result)
    {
        const auto item = m_item;
        QMetaObject::invokeMethod(
                m_service,
                [item, result]() {
                    if (!item->cancelled.load()) {
                        item->onFinished();
                        item->onResult(item->request, result);
                    }
                },
                Qt::QueuedConnection);
    }

    void deliverError(const core::Error &error)
    {
        const auto item = m_item;
        QMetaObject::invokeMethod(
                m_service,
                [item, error]() {
                    if (!item->cancelled.load()) {
                        item->onFinished();
                        item->onError(item->request, error);
                    }
                },
                Qt::QueuedConnection);
    }

    std::shared_ptr<PendingItem> m_item;
    quint64 m_handle;
    ThumbnailDiskCache *m_cache;
    ThumbnailRenderer *m_renderer;
    QObject *m_service;
};

class ThumbnailService::Private
{
public:
    ThumbnailDiskCache *cache = nullptr;
    ThumbnailRenderer *renderer = nullptr;
    int maxConcurrency = 2;

    QThreadPool pool;
    // m_pending is only accessed on the owning thread.
    QHash<quint64, std::shared_ptr<PendingItem>> pending;
    quint64 nextHandle = 1;
};

ThumbnailService::ThumbnailService(ThumbnailDiskCache *cache, ThumbnailRenderer *renderer,
                                   QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->cache = cache;
    d->renderer = renderer;
    d->pool.setMaxThreadCount(d->maxConcurrency);
}

ThumbnailService::~ThumbnailService()
{
    // Cancel all pending work so runnables do not deliver to a dead object.
    for (auto &item : d->pending) {
        item->cancelled.store(true);
    }
    d->pool.waitForDone();
}

void ThumbnailService::setMaxConcurrency(int n)
{
    d->maxConcurrency = qMax(1, n);
    d->pool.setMaxThreadCount(d->maxConcurrency);
}

int ThumbnailService::maxConcurrency() const
{
    return d->maxConcurrency;
}

core::MediaRequestHandle ThumbnailService::request(const core::MediaRequest &request,
                                                    ResultCallback onResult,
                                                    ErrorCallback onError)
{
    const quint64 handleValue = d->nextHandle++;
    const core::MediaRequestHandle handle(handleValue);

    auto item = std::make_shared<PendingItem>();
    item->request = request;
    item->onResult = std::move(onResult);
    item->onError = std::move(onError);
    item->onFinished = [this, handleValue]() { d->pending.remove(handleValue); };

    d->pending.insert(handleValue, item);

    const int priority = poolPriority(request.priority);
    auto *runnable = new ThumbnailRunnable(item, handleValue, d->cache, d->renderer, this);
    d->pool.start(runnable, priority);

    return handle;
}

void ThumbnailService::cancel(core::MediaRequestHandle handle)
{
    const auto it = d->pending.find(handle.value());
    if (it == d->pending.end()) {
        return;
    }
    it.value()->cancelled.store(true);
    d->pending.erase(it);
}

void ThumbnailService::cancelAllExcept(const QList<core::MediaRequestHandle> &keep)
{
    QList<quint64> keepValues;
    keepValues.reserve(keep.size());
    for (const core::MediaRequestHandle &h : keep) {
        keepValues.append(h.value());
    }

    const QList<quint64> handles = d->pending.keys();
    for (quint64 h : handles) {
        if (!keepValues.contains(h)) {
            cancel(core::MediaRequestHandle(h));
        }
    }
}

int ThumbnailService::pendingCount() const
{
    return d->pending.size();
}

} // namespace pimio::thumbnail
