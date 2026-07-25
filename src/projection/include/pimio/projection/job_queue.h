#pragma once

#include "pimio/core/error.h"
#include "pimio/core/job.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace pimio::projection {

/// Persistent priority job queue backed by SQLite.
///
/// The queue is the durable record of work to be done and is independent of
/// the disposable media projection. Deleting the projection cache does not
/// lose pending jobs.
///
/// Usage sequence after open():
///   1. Call recoverInterruptedJobs() to reset Running jobs left by a previous
///      session back to Pending.
///   2. Call claim() to acquire a batch of Pending jobs.
///   3. Execute the claimed jobs and call markSucceeded(), markFailed(), or
///      markCancelled() to record the outcome.
class JobQueue
{
public:
    JobQueue();
    ~JobQueue();

    JobQueue(const JobQueue &) = delete;
    JobQueue &operator=(const JobQueue &) = delete;

    /// Opens (creating if needed) the queue at \a path.
    bool open(const QString &path, core::Error *error);

    /// Opens a private in-memory queue for tests.
    bool openInMemory(core::Error *error);

    void close();
    bool isOpen() const;
    const QString &path() const;

    /// Resets Running jobs left by a previous session back to Pending.
    ///
    /// A Running job that survived a restart was interrupted mid-execution.
    /// Call this once immediately after open() so those jobs are retried rather
    /// than silently abandoned. It is safe to call on a freshly created queue.
    bool recoverInterruptedJobs(core::Error *error);

    /// Adds a job to the queue and returns its id.
    ///
    /// If \a record has a non-empty coalescingKey and a non-terminal job with
    /// that key already exists, returns the existing job's id without inserting
    /// a duplicate. This enforces the "run once logically" contract.
    std::optional<core::JobId> enqueue(const core::JobRecord &record, core::Error *error);

    /// Claims up to \a maxJobs pending, eligible jobs in priority order and
    /// transitions them to Running. A job is eligible when its notBefore time
    /// has passed or is not set.
    QList<core::JobRecord> claim(int maxJobs, core::Error *error);

    /// Transitions a Running job to Succeeded.
    bool markSucceeded(const core::JobId &id, core::Error *error);

    /// Records \a failure on the job. If the failure is retryable and the job
    /// has attempts remaining, transitions it back to Pending with an
    /// incremented attempt count. Otherwise transitions to Failed.
    bool markFailed(const core::JobId &id, const core::Error &failure, core::Error *error);

    /// Transitions a Pending or Running job to Cancelled.
    bool markCancelled(const core::JobId &id, core::Error *error);

    /// Returns the record for \a id, or std::nullopt when not found.
    std::optional<core::JobRecord> load(const core::JobId &id, core::Error *error) const;

    /// Returns all Pending jobs in claim order: priority first, then creation
    /// time, then id.
    QList<core::JobRecord> listPending(core::Error *error) const;

    qsizetype pendingCount(core::Error *error) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::projection
