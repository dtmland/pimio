#include "pimio/watch/watch_service.h"

#include "pimio/watch/reconcile_worker.h"

namespace pimio::watch {

namespace {
/// How often the internal timer wakes up to check whether a coalesced batch
/// or the periodic fallback has become due. Small relative to the debounce
/// window so debounced reconciles fire promptly without busy-polling.
constexpr int kTickIntervalMs = 100;
} // namespace

WatchService::WatchService(WatchAdapter *adapter, projection::JobQueue *jobQueue, QObject *parent)
    : QObject(parent)
    , m_adapter(adapter)
    , m_jobQueue(jobQueue)
    , m_coalescer(m_debounceMs)
{
    Q_ASSERT(m_adapter != nullptr);
    Q_ASSERT(m_jobQueue != nullptr);

    connect(m_adapter, &WatchAdapter::eventOccurred, this, &WatchService::onAdapterEvent);
    connect(&m_timer, &QTimer::timeout, this, &WatchService::onTimerTick);
}

WatchService::~WatchService()
{
    stop();
}

void WatchService::setDebounceMs(int ms)
{
    m_debounceMs = ms;
}

void WatchService::setPeriodicFallbackIntervalMs(qint64 ms)
{
    m_periodicFallbackIntervalMs = ms;
}

bool WatchService::start(const scan::LibraryRoot &root, core::Error *error)
{
    stop();

    if (!m_adapter->start(root.absolutePath, error)) {
        return false;
    }

    m_root = root;
    m_coalescer = EventCoalescer(m_debounceMs);
    m_lastReconcileAt = QDateTime::currentDateTimeUtc();
    m_active = true;
    m_timer.start(kTickIntervalMs);
    return true;
}

void WatchService::stop()
{
    if (!m_active) {
        return;
    }
    m_timer.stop();
    m_adapter->stop();
    m_active = false;
}

bool WatchService::isActive() const
{
    return m_active;
}

void WatchService::onAdapterEvent(const WatchEvent &event)
{
    if (!m_active) {
        return;
    }
    m_coalescer.ingest(event, QDateTime::currentDateTimeUtc());
}

void WatchService::onTimerTick()
{
    if (!m_active) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (m_coalescer.takeDueReconcile(now)) {
        enqueueReconcile(m_coalescer.lastReconcileWasOverflow(), /*periodic=*/false);
        return;
    }

    if (EventCoalescer::isPeriodicFallbackDue(m_lastReconcileAt, now,
                                              m_periodicFallbackIntervalMs)) {
        enqueueReconcile(/*overflow=*/false, /*periodic=*/true);
    }
}

void WatchService::enqueueReconcile(bool overflow, bool periodic)
{
    core::JobRecord record;
    record.kind = core::JobKind::ReconcileRoot;
    record.priority = core::JobPriority::Background;
    record.coalescingKey = QStringLiteral("watch-reconcile:%1").arg(m_root.absolutePath);
    record.payload = makeRootJobPayload(m_root);
    record.payload.insert(QStringLiteral("triggeredByOverflow"), overflow);
    record.payload.insert(QStringLiteral("triggeredByPeriodicFallback"), periodic);

    core::Error error;
    const std::optional<core::JobId> id = m_jobQueue->enqueue(record, &error);

    m_lastReconcileAt = QDateTime::currentDateTimeUtc();
    emit reconcileEnqueued(id ? id->value() : QString(), overflow, periodic);
}

} // namespace pimio::watch
