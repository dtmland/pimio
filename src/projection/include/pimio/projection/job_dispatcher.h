#pragma once

#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/projection/job_queue.h"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

namespace pimio::projection {

/// Drives execution of queued jobs with bounded concurrency.
///
/// Workers are registered per JobKind before start(). The dispatcher polls
/// the queue for pending work, dispatches up to maxConcurrency jobs at once
/// on a private thread pool, and writes the outcome back to the queue.
///
/// Cancellation is cooperative: calling requestCancellation() sets an atomic
/// flag that the worker receives as its second argument. Workers that observe
/// the flag should return Error::cancelled() promptly.
class JobDispatcher : public QObject
{
    Q_OBJECT

public:
    /// A worker callable. The second argument is set when the dispatcher has
    /// been asked to cancel this job. Workers should poll it periodically and
    /// return Error::cancelled() when set.
    using WorkerFn = std::function<core::Error(const core::JobRecord &,
                                               const std::atomic<bool> &isCancelled)>;

    explicit JobDispatcher(JobQueue *queue, QObject *parent = nullptr);
    ~JobDispatcher() override;

    /// Registers a worker function for \a kind. Must be called before start().
    void registerWorker(core::JobKind kind, WorkerFn fn);

    /// Sets the maximum number of concurrently running jobs. Default: 1.
    void setMaxConcurrency(int n);
    int maxConcurrency() const;

    /// Starts dispatching. The timer begins polling the queue immediately.
    void start();

    /// Stops dispatching and waits for all in-flight jobs to complete.
    void stop();

    /// Sets the cancellation flag for a running job. Has no effect if \a id
    /// is not currently running.
    void requestCancellation(const core::JobId &id);

    /// Number of jobs currently dispatched to worker threads.
    int runningCount() const;

signals:
    /// Emitted on the dispatcher's thread when a job starts running.
    void jobStarted(QString jobId);
    /// Emitted when a job finishes successfully.
    void jobSucceeded(QString jobId);
    /// Emitted when a job finishes with a failure (may be re-queued for retry).
    void jobFailed(QString jobId);
    /// Emitted when a job is cancelled.
    void jobCancelled(QString jobId);

private slots:
    void tryDispatch();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::projection
