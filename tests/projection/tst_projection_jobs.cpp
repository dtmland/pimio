#include "pimio/projection/job_dispatcher.h"
#include "pimio/projection/job_queue.h"

#include "pimio/testing/qtest_printers.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::core;
using namespace pimio::projection;

namespace {

JobRecord makeRecord(JobKind kind, JobPriority priority, const QString &coalescingKey = {})
{
    JobRecord record;
    record.id = JobId::generate();
    record.kind = kind;
    record.priority = priority;
    record.coalescingKey = coalescingKey;
    record.maxAttempts = 3;
    record.createdAt = QDateTime::currentDateTimeUtc();
    return record;
}

// Helper to collect job ids as plain strings for comparison.
QList<QString> idValues(const QList<JobRecord> &records)
{
    QList<QString> values;
    values.reserve(records.size());
    for (const JobRecord &r : records) {
        values.append(r.id.value());
    }
    return values;
}

} // namespace

class TestProjectionJobs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- JobQueue: persistence -----------------------------------------------
    void jobsSurviveRestart();
    void interruptedRunningJobsAreRestoredOnReopen();

    // ---- JobQueue: coalescing ------------------------------------------------
    void enqueuingWithTheSameCoalescingKeyDoesNotInsertADuplicate();
    void runningJobAllowsOneCoalescedFollowUp();
    void terminalJobsDoNotBlockSubsequentCoalescing();

    // ---- JobQueue: priority ordering ----------------------------------------
    void jobsAreClaimedInPriorityOrder();

    // ---- JobQueue: state transitions ----------------------------------------
    void claimedJobIsMarkedRunning();
    void markSucceededRemovesJobFromPending();
    void markCancelledRemovesJobFromPending();
    void markFailedWithRetriesRemainingRequeues();
    void markFailedExhaustingRetriesTerminates();
    void markFailedWithNonRetryableErrorTerminatesImmediately();

    // ---- JobDispatcher -------------------------------------------------------
    void dispatcherRunsRegisteredWorkerAndMarksSucceeded();
    void dispatcherRetriesRetryableFailures();
    void dispatcherHonoursCancellationToken();
    void dispatcherLimitsConcurrency();
};

void TestProjectionJobs::initTestCase()
{
    QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
             "the Qt SQLite driver must be present");
}

// ---- JobQueue: persistence --------------------------------------------------

void TestProjectionJobs::jobsSurviveRestart()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("jobs.db"));

    QString id1, id2, id3;
    {
        JobQueue queue;
        Error error;
        QVERIFY2(queue.open(path, &error), qPrintable(error.message()));

        auto r = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
        QVERIFY2(r.has_value(), qPrintable(error.message()));
        id1 = r->value();

        r = queue.enqueue(makeRecord(JobKind::ReadMetadata, JobPriority::UserInitiated), &error);
        QVERIFY2(r.has_value(), qPrintable(error.message()));
        id2 = r->value();

        r = queue.enqueue(makeRecord(JobKind::GenerateThumbnail, JobPriority::Interactive),
                          &error);
        QVERIFY2(r.has_value(), qPrintable(error.message()));
        id3 = r->value();
    }

    // Re-open as a new application session would.
    JobQueue reopened;
    Error error;
    QVERIFY2(reopened.open(path, &error), qPrintable(error.message()));
    QVERIFY2(reopened.recoverInterruptedJobs(&error), qPrintable(error.message()));

    QCOMPARE(reopened.pendingCount(&error), 3);

    const QList<JobRecord> pending = reopened.listPending(&error);
    const QList<QString> ids = idValues(pending);
    QVERIFY(ids.contains(id1));
    QVERIFY(ids.contains(id2));
    QVERIFY(ids.contains(id3));
}

void TestProjectionJobs::interruptedRunningJobsAreRestoredOnReopen()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("jobs.db"));

    QString id1, id2;
    {
        JobQueue queue;
        Error error;
        QVERIFY2(queue.open(path, &error), qPrintable(error.message()));

        auto r1 = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
        QVERIFY(r1.has_value());
        id1 = r1->value();

        auto r2 = queue.enqueue(makeRecord(JobKind::ReadMetadata, JobPriority::Background), &error);
        QVERIFY(r2.has_value());
        id2 = r2->value();

        // Claim without recording any outcome: simulates a process crash.
        const QList<JobRecord> claimed = queue.claim(2, &error);
        QCOMPARE(claimed.size(), 2);
        for (const JobRecord &rec : claimed) {
            PIMIO_COMPARE_ENUM(rec.state, JobState::Running);
        }
        // queue goes out of scope here with both jobs in Running state.
    }

    JobQueue reopened;
    Error error;
    QVERIFY2(reopened.open(path, &error), qPrintable(error.message()));

    // Before recovery the jobs are still stored as Running.
    const auto r1before = reopened.load(JobId(id1), &error);
    QVERIFY(r1before.has_value());
    PIMIO_COMPARE_ENUM(r1before->state, JobState::Running);

    QVERIFY2(reopened.recoverInterruptedJobs(&error), qPrintable(error.message()));

    // After recovery they are Pending and ready to be re-dispatched.
    QCOMPARE(reopened.pendingCount(&error), 2);
    const auto r1after = reopened.load(JobId(id1), &error);
    QVERIFY(r1after.has_value());
    PIMIO_COMPARE_ENUM(r1after->state, JobState::Pending);
}

// ---- JobQueue: coalescing ---------------------------------------------------

void TestProjectionJobs::enqueuingWithTheSameCoalescingKeyDoesNotInsertADuplicate()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const QString key = QStringLiteral("scan:root-1");
    JobRecord first = makeRecord(JobKind::ScanRoot, JobPriority::Background, key);
    const auto id1 = queue.enqueue(first, &error);
    QVERIFY2(id1.has_value(), qPrintable(error.message()));

    // Second enqueue with the same coalescing key must return the existing id.
    JobRecord second = makeRecord(JobKind::ScanRoot, JobPriority::Background, key);
    const auto id2 = queue.enqueue(second, &error);
    QVERIFY2(id2.has_value(), qPrintable(error.message()));
    QCOMPARE(id2->value(), id1->value());

    // Only one job exists.
    QCOMPARE(queue.pendingCount(&error), 1);
}

void TestProjectionJobs::runningJobAllowsOneCoalescedFollowUp()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const QString key = QStringLiteral("scan:root-1");
    const auto runningId =
            queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background, key), &error);
    QVERIFY(runningId.has_value());
    QCOMPARE(queue.claim(1, &error).size(), 1);

    const auto followUpId =
            queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background, key), &error);
    QVERIFY2(followUpId.has_value(), qPrintable(error.message()));
    QVERIFY(followUpId->value() != runningId->value());

    const auto duplicateId =
            queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background, key), &error);
    QVERIFY2(duplicateId.has_value(), qPrintable(error.message()));
    QCOMPARE(duplicateId->value(), followUpId->value());
    QCOMPARE(queue.pendingCount(&error), 1);
}

void TestProjectionJobs::terminalJobsDoNotBlockSubsequentCoalescing()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const QString key = QStringLiteral("scan:root-1");
    const auto id1 = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background, key),
                                   &error);
    QVERIFY(id1.has_value());

    // Claim and complete the first job.
    const auto claimed = queue.claim(1, &error);
    QCOMPARE(claimed.size(), 1);
    QVERIFY2(queue.markSucceeded(id1.value(), &error), qPrintable(error.message()));

    // A new enqueue with the same key should now create a fresh job, not
    // return the completed one.
    const auto id2 = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background, key),
                                   &error);
    QVERIFY2(id2.has_value(), qPrintable(error.message()));
    QVERIFY(id2->value() != id1->value());
    QCOMPARE(queue.pendingCount(&error), 1);
}

// ---- JobQueue: priority ordering --------------------------------------------

void TestProjectionJobs::jobsAreClaimedInPriorityOrder()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    // Enqueue in deliberately wrong order.
    const auto bg = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
    QVERIFY(bg.has_value());
    const auto opp =
        queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Opportunistic), &error);
    QVERIFY(opp.has_value());
    const auto ui =
        queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::UserInitiated), &error);
    QVERIFY(ui.has_value());
    const auto interactive =
        queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Interactive), &error);
    QVERIFY(interactive.has_value());

    const QList<JobRecord> claimed = queue.claim(4, &error);
    QCOMPARE(claimed.size(), 4);

    // Priority order: Interactive < UserInitiated < Background < Opportunistic
    QCOMPARE(claimed[0].id.value(), interactive->value());
    QCOMPARE(claimed[1].id.value(), ui->value());
    QCOMPARE(claimed[2].id.value(), bg->value());
    QCOMPARE(claimed[3].id.value(), opp->value());
}

// ---- JobQueue: state transitions --------------------------------------------

void TestProjectionJobs::claimedJobIsMarkedRunning()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const auto id = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
    QVERIFY(id.has_value());

    const QList<JobRecord> claimed = queue.claim(1, &error);
    QCOMPARE(claimed.size(), 1);
    PIMIO_COMPARE_ENUM(claimed[0].state, JobState::Running);

    // Load from storage to verify the state was persisted, not just returned.
    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Running);
}

void TestProjectionJobs::markSucceededRemovesJobFromPending()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const auto id = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
    QVERIFY(id.has_value());

    const QList<JobRecord> claimed = queue.claim(1, &error);
    QCOMPARE(claimed.size(), 1);
    QVERIFY2(queue.markSucceeded(id.value(), &error), qPrintable(error.message()));

    QCOMPARE(queue.pendingCount(&error), 0);
    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Succeeded);
}

void TestProjectionJobs::markCancelledRemovesJobFromPending()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const auto id = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
    QVERIFY(id.has_value());

    // Cancel without even claiming.
    QVERIFY2(queue.markCancelled(id.value(), &error), qPrintable(error.message()));
    QCOMPARE(queue.pendingCount(&error), 0);
    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Cancelled);
}

void TestProjectionJobs::markFailedWithRetriesRemainingRequeues()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    JobRecord record = makeRecord(JobKind::ReadMetadata, JobPriority::Background);
    record.maxAttempts = 3;
    const auto id = queue.enqueue(record, &error);
    QVERIFY(id.has_value());

    const Error retryableFailure(ErrorCode::Timeout, QStringLiteral("timed out"));
    QVERIFY(retryableFailure.isRetryable());

    // First failure: attempts 0 → 1, 1 < 3 → re-queued.
    queue.claim(1, &error);
    QVERIFY2(queue.markFailed(id.value(), retryableFailure, &error), qPrintable(error.message()));
    QCOMPARE(queue.pendingCount(&error), 1);

    const auto afterFirst = queue.load(id.value(), &error);
    QVERIFY(afterFirst.has_value());
    PIMIO_COMPARE_ENUM(afterFirst->state, JobState::Pending);
    QCOMPARE(afterFirst->attempts, 1);

    // Second failure: attempts 1 → 2, 2 < 3 → re-queued.
    queue.claim(1, &error);
    QVERIFY2(queue.markFailed(id.value(), retryableFailure, &error), qPrintable(error.message()));
    QCOMPARE(queue.pendingCount(&error), 1);

    // Third failure: attempts 2 → 3, 3 is not < 3 → terminal.
    queue.claim(1, &error);
    QVERIFY2(queue.markFailed(id.value(), retryableFailure, &error), qPrintable(error.message()));
    QCOMPARE(queue.pendingCount(&error), 0);

    const auto afterThird = queue.load(id.value(), &error);
    QVERIFY(afterThird.has_value());
    PIMIO_COMPARE_ENUM(afterThird->state, JobState::Failed);
    QCOMPARE(afterThird->attempts, 3);
}

void TestProjectionJobs::markFailedExhaustingRetriesTerminates()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    JobRecord record = makeRecord(JobKind::ReadMetadata, JobPriority::Background);
    record.maxAttempts = 1; // only one allowed attempt
    const auto id = queue.enqueue(record, &error);
    QVERIFY(id.has_value());

    queue.claim(1, &error);
    const Error retryableFailure(ErrorCode::Timeout, QStringLiteral("timed out"));
    QVERIFY2(queue.markFailed(id.value(), retryableFailure, &error), qPrintable(error.message()));

    // With maxAttempts=1: attempts goes 0→1, 1 is not < 1 → failed immediately.
    QCOMPARE(queue.pendingCount(&error), 0);
    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Failed);
}

void TestProjectionJobs::markFailedWithNonRetryableErrorTerminatesImmediately()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    JobRecord record = makeRecord(JobKind::ReadMetadata, JobPriority::Background);
    record.maxAttempts = 3;
    const auto id = queue.enqueue(record, &error);
    QVERIFY(id.has_value());

    queue.claim(1, &error);
    const Error nonRetryable(ErrorCode::UnsupportedMedia, QStringLiteral("bad format"));
    QVERIFY(!nonRetryable.isRetryable());

    QVERIFY2(queue.markFailed(id.value(), nonRetryable, &error), qPrintable(error.message()));
    QCOMPARE(queue.pendingCount(&error), 0);

    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Failed);
    QCOMPARE(loaded->attempts, 1);
}

// ---- JobDispatcher ----------------------------------------------------------

void TestProjectionJobs::dispatcherRunsRegisteredWorkerAndMarksSucceeded()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const auto id = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
    QVERIFY(id.has_value());

    bool workerCalled = false;
    JobDispatcher dispatcher(&queue);
    dispatcher.registerWorker(JobKind::ScanRoot, [&](const JobRecord &, const std::atomic<bool> &) {
        workerCalled = true;
        return Error();
    });
    dispatcher.start();
    dispatcher.stop();

    QVERIFY(workerCalled);
    QCOMPARE(queue.pendingCount(&error), 0);

    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Succeeded);
}

void TestProjectionJobs::dispatcherRetriesRetryableFailures()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    JobRecord record = makeRecord(JobKind::ReadMetadata, JobPriority::Background);
    record.maxAttempts = 3;
    const auto id = queue.enqueue(record, &error);
    QVERIFY(id.has_value());

    int callCount = 0;
    JobDispatcher dispatcher(&queue);
    dispatcher.registerWorker(
        JobKind::ReadMetadata, [&](const JobRecord &, const std::atomic<bool> &) -> Error {
            ++callCount;
            if (callCount < 3) {
                return Error(ErrorCode::Timeout, QStringLiteral("transient"));
            }
            return Error(); // succeeds on the third attempt
        });
    dispatcher.start();
    dispatcher.stop();

    QCOMPARE(callCount, 3);
    QCOMPARE(queue.pendingCount(&error), 0);

    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Succeeded);
    QCOMPARE(loaded->attempts, 3);
}

void TestProjectionJobs::dispatcherHonoursCancellationToken()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    const auto id = queue.enqueue(makeRecord(JobKind::ScanRoot, JobPriority::Background), &error);
    QVERIFY(id.has_value());

    JobDispatcher dispatcher(&queue);
    dispatcher.registerWorker(
        JobKind::ScanRoot, [&](const JobRecord &rec, const std::atomic<bool> &isCancelled) {
            dispatcher.requestCancellation(rec.id);
            // Worker observes the flag and exits early.
            if (isCancelled.load()) {
                return Error::cancelled();
            }
            return Error();
        });
    dispatcher.start();
    dispatcher.stop();

    QCOMPARE(queue.pendingCount(&error), 0);
    const auto loaded = queue.load(id.value(), &error);
    QVERIFY(loaded.has_value());
    PIMIO_COMPARE_ENUM(loaded->state, JobState::Cancelled);
}

void TestProjectionJobs::dispatcherLimitsConcurrency()
{
    JobQueue queue;
    Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    // Enqueue more jobs than the concurrency limit.
    for (int i = 0; i < 6; ++i) {
        QVERIFY(queue.enqueue(makeRecord(JobKind::GenerateThumbnail, JobPriority::Background),
                              &error)
                    .has_value());
    }

    QMutex mutex;
    int peakConcurrent = 0;
    int currentConcurrent = 0;

    JobDispatcher dispatcher(&queue);
    dispatcher.setMaxConcurrency(2);
    dispatcher.registerWorker(
        JobKind::GenerateThumbnail,
        [&](const JobRecord &, const std::atomic<bool> &) -> Error {
            {
                QMutexLocker lock(&mutex);
                ++currentConcurrent;
                if (currentConcurrent > peakConcurrent) {
                    peakConcurrent = currentConcurrent;
                }
            }
            QThread::msleep(10);
            {
                QMutexLocker lock(&mutex);
                --currentConcurrent;
            }
            return Error();
        });
    dispatcher.start();
    dispatcher.stop();

    QCOMPARE(queue.pendingCount(&error), 0);
    QVERIFY2(peakConcurrent <= 2,
             qPrintable(QStringLiteral("peak concurrent was %1, expected <= 2")
                            .arg(peakConcurrent)));
}

QTEST_MAIN(TestProjectionJobs)

#include "tst_projection_jobs.moc"
