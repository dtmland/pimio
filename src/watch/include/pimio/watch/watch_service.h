#pragma once

#include "pimio/watch/event_coalescer.h"
#include "pimio/watch/watch_adapter.h"

#include "pimio/core/error.h"
#include "pimio/projection/job_queue.h"
#include "pimio/scan/library_root.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

namespace pimio::watch {

/// Glues a WatchAdapter, an EventCoalescer, and a durable JobQueue together
/// into the running watch pipeline described by Increment 7.
///
/// Raw events from the adapter are coalesced (see EventCoalescer) before
/// becoming a durable pimio::core::JobKind::ReconcileRoot job at
/// core::JobPriority::Background, so a burst of filesystem activity produces
/// one queued job rather than one per event. The job's coalescingKey is the
/// root path, so JobQueue itself additionally collapses a reconcile that is
/// still pending when another one becomes due — the watch-level and
/// job-queue-level coalescing reinforce each other rather than duplicating
/// logic.
///
/// A periodic timer independently requests a reconcile on a much longer
/// interval regardless of whether any event was observed at all. This is
/// the recovery path for a channel that drops an event silently, with no
/// overflow signal — the adapter cannot report what it never saw, so only a
/// schedule that does not depend on any signal can recover from it.
class WatchService : public QObject
{
    Q_OBJECT

public:
    /// \a adapter and \a jobQueue must outlive this object. Neither pointer
    /// may be null.
    WatchService(WatchAdapter *adapter, projection::JobQueue *jobQueue, QObject *parent = nullptr);
    ~WatchService() override;

    /// How long to wait after the first pending change before enqueuing a
    /// reconcile job. Must be set before start(). Default: 500 ms.
    void setDebounceMs(int ms);

    /// How often to request a reconcile even without any observed event, as
    /// a safety net against silently dropped notifications. Must be set
    /// before start(). Default: 15 minutes.
    void setPeriodicFallbackIntervalMs(qint64 ms);

    /// Starts watching \a root and begins the periodic fallback schedule.
    bool start(const scan::LibraryRoot &root, core::Error *error);

    void stop();
    bool isActive() const;

signals:
    /// Emitted right after a reconcile job is (re-)enqueued, whether the
    /// trigger was coalesced events, an overflow, or the periodic fallback.
    /// \a jobId is invalid when JobQueue::enqueue() itself failed.
    void reconcileEnqueued(QString jobId, bool wasOverflow, bool wasPeriodicFallback);

private slots:
    void onAdapterEvent(const pimio::watch::WatchEvent &event);
    void onTimerTick();

private:
    void enqueueReconcile(bool overflow, bool periodic);

    WatchAdapter *m_adapter;
    projection::JobQueue *m_jobQueue;
    EventCoalescer m_coalescer;
    int m_debounceMs = 500;
    scan::LibraryRoot m_root;
    QDateTime m_lastReconcileAt;
    qint64 m_periodicFallbackIntervalMs = 15LL * 60 * 1000;
    QTimer m_timer;
    bool m_active = false;
};

} // namespace pimio::watch
