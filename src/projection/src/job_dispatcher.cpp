#include "pimio/projection/job_dispatcher.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QTimer>

namespace pimio::projection {

using core::Error;
using core::JobId;
using core::JobKind;
using core::JobRecord;

class JobDispatcher::Private
{
public:
    JobQueue *queue = nullptr;
    QHash<JobKind, WorkerFn> workers;
    int maxConcurrency = 1;

    QTimer *pollTimer = nullptr;
    bool running = false;

    // Protected by mutex so requestCancellation() is safe from any thread.
    mutable QMutex mutex;
    QHash<QString, std::shared_ptr<std::atomic<bool>>> cancellations;
    int inflightCount = 0;

    // Dedicated thread pool so test instances do not compete with each other
    // or with QThreadPool::globalInstance().
    QThreadPool pool;
};

JobDispatcher::JobDispatcher(JobQueue *queue, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->queue = queue;
    d->pollTimer = new QTimer(this);
    d->pollTimer->setInterval(50);
    d->pollTimer->setSingleShot(false);
    connect(d->pollTimer, &QTimer::timeout, this, &JobDispatcher::tryDispatch);
}

JobDispatcher::~JobDispatcher()
{
    stop();
}

void JobDispatcher::registerWorker(JobKind kind, WorkerFn fn)
{
    d->workers.insert(kind, std::move(fn));
}

void JobDispatcher::setMaxConcurrency(int n)
{
    d->maxConcurrency = qMax(1, n);
    d->pool.setMaxThreadCount(d->maxConcurrency);
}

int JobDispatcher::maxConcurrency() const
{
    return d->maxConcurrency;
}

void JobDispatcher::start()
{
    if (d->running) {
        return;
    }
    d->running = true;
    d->pollTimer->start();
    // Dispatch immediately rather than waiting for the first timer tick.
    tryDispatch();
}

void JobDispatcher::stop()
{
    if (!d->running) {
        return;
    }
    d->pollTimer->stop();

    // Drain all pending and in-flight work before returning. Completion
    // callbacks (delivered via Qt::QueuedConnection) may re-queue jobs when
    // retrying, so we loop: wait for in-flight to reach zero, then check for
    // newly-pending retried jobs and dispatch them before checking again.
    // Workers post completions via QueuedConnection, so the event loop must
    // be spinning; processEvents delivers those callbacks inline.
    for (;;) {
        {
            QMutexLocker lock(&d->mutex);
            if (d->inflightCount > 0) {
                lock.unlock();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                continue;
            }
        }
        // All in-flight complete. Check for retried or newly-pending jobs.
        Error err;
        const qsizetype pending =
            (d->queue && d->queue->isOpen()) ? d->queue->pendingCount(&err) : 0;
        if (pending <= 0) {
            break;
        }
        // Dispatch the pending work and loop back to wait for it to finish.
        tryDispatch();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    d->running = false;
}

void JobDispatcher::requestCancellation(const JobId &id)
{
    QMutexLocker lock(&d->mutex);
    auto it = d->cancellations.find(id.value());
    if (it != d->cancellations.end()) {
        it.value()->store(true, std::memory_order_relaxed);
    }
}

int JobDispatcher::runningCount() const
{
    QMutexLocker lock(&d->mutex);
    return d->inflightCount;
}

void JobDispatcher::tryDispatch()
{
    if (!d->running || !d->queue || !d->queue->isOpen()) {
        return;
    }

    int availableSlots;
    {
        QMutexLocker lock(&d->mutex);
        availableSlots = d->maxConcurrency - d->inflightCount;
    }
    if (availableSlots <= 0) {
        return;
    }

    Error claimError;
    const QList<JobRecord> jobs = d->queue->claim(availableSlots, &claimError);
    if (claimError.isError()) {
        return;
    }

    for (const JobRecord &job : jobs) {
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        {
            QMutexLocker lock(&d->mutex);
            d->cancellations.insert(job.id.value(), cancel);
            ++d->inflightCount;
        }
        emit jobStarted(job.id.value());

        // The lambda captures by value so it is self-contained on the pool
        // thread. 'priv' is valid for the duration of the dispatcher: stop()
        // drains all work before returning.
        Private *priv = d.get();
        JobQueue *queue = d->queue;
        QHash<JobKind, WorkerFn> workers = d->workers;

        d->pool.start([this, job, cancel, priv, queue, workers]() {
            Error result;
            auto it = workers.find(job.kind);
            if (it != workers.end()) {
                result = it.value()(job, *cancel);
            } else {
                result = Error(core::ErrorCode::Internal,
                               QStringLiteral("No worker registered for job kind %1.")
                                   .arg(core::toString(job.kind)));
            }

            const bool wasCancelled = cancel->load(std::memory_order_relaxed);

            // All queue mutations happen on the dispatcher's thread (where the
            // SQLite connection was opened). Workers must not touch it directly.
            QMetaObject::invokeMethod(this, [this, priv, queue, job, wasCancelled, result]() {
                Error queueError;
                if (wasCancelled) {
                    queue->markCancelled(job.id, &queueError);
                } else if (result.isError()) {
                    queue->markFailed(job.id, result, &queueError);
                } else {
                    queue->markSucceeded(job.id, &queueError);
                }

                {
                    QMutexLocker lock(&priv->mutex);
                    priv->cancellations.remove(job.id.value());
                    --priv->inflightCount;
                }

                if (wasCancelled) {
                    emit jobCancelled(job.id.value());
                } else if (result.isError()) {
                    emit jobFailed(job.id.value());
                } else {
                    emit jobSucceeded(job.id.value());
                }

                // A slot is now free. Pick up the next job immediately rather
                // than waiting for the next timer tick.
                tryDispatch();
            }, Qt::QueuedConnection);
        });
    }
}

} // namespace pimio::projection
