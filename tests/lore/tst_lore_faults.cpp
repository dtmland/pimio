#include "tst_lore_faults_fixture.h"

#include "lore_test_support.h"

#include "pimio/lore/lore_durable_store.h"
#include "pimio/testing/qtest_printers.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::core;
using namespace pimio::lore;
using namespace pimio::testing;

void TestLoreFaults::initTestCase()
{
    m_helperPath = QCoreApplication::applicationDirPath() + QStringLiteral("/pimio_lore_fault_helper");
#ifdef Q_OS_WIN
    m_helperPath += QStringLiteral(".exe");
#endif
}

void TestLoreFaults::unwritableCheckoutFailsVisiblyAndKeepsStagedWork()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("kept"), QStringLiteral("committed")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Baseline"), &error).has_value(),
             qPrintable(error.message()));
    const QString tokenBefore = store.stateToken();

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("blocked"), QStringLiteral("cannot land")),
                        &error));

    // Losing write access mid-session models a permission change, a read-only
    // remount, and the write failures a full volume produces. A genuinely full
    // volume cannot be simulated portably; see the decision record.
    const QString shard = storePath + QStringLiteral("/repository/records/bl");
    QVERIFY(QDir().mkpath(shard));
    if (!makeReadOnly(shard)) {
        QSKIP("This filesystem does not honour a read-only directory.");
    }
    QFile probe(shard + QStringLiteral("/probe"));
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        makeWritableAgain(shard);
        QSKIP("This filesystem does not enforce a read-only directory.");
    }

    Error commitError;
    const auto checkpoint = store.commit(QStringLiteral("Blocked save"), &commitError);
    makeWritableAgain(shard);

    QVERIFY(!checkpoint.has_value());
    QVERIFY(commitError.isError());
    QVERIFY(commitError.code() == ErrorCode::PermissionDenied
            || commitError.code() == ErrorCode::OutOfSpace
            || commitError.code() == ErrorCode::Internal);

    // The failed save changed nothing and lost nothing.
    QCOMPARE(store.stateToken(), tokenBefore);
    QCOMPARE(store.listIds(nullptr).size(), 1);
    QVERIFY(!store.load(MediaId(QStringLiteral("blocked")), nullptr).has_value());
    QCOMPARE(store.history(-1, nullptr).size(), 1);
    QVERIFY(store.hasStagedChanges());
    QCOMPARE(stagedFileCount(storePath), 1);

    // Once the fault clears, the same staged work commits successfully.
    QVERIFY2(store.commit(QStringLiteral("Retry"), &error).has_value(),
             qPrintable(error.message()));
    QVERIFY(store.load(MediaId(QStringLiteral("blocked")), nullptr).has_value());
    QVERIFY(store.stateToken() != tokenBefore);
}

void TestLoreFaults::deletingTheCheckoutLosesNoCommittedState()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    for (int index = 0; index < 8; ++index) {
        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("rec-%1").arg(index),
                                           QStringLiteral("caption %1").arg(index)),
                            &error));
    }
    QVERIFY2(store.commit(QStringLiteral("Everything"), &error).has_value(),
             qPrintable(error.message()));

    const QList<MediaId> before = store.listIds(nullptr);
    const QString tokenBefore = store.stateToken();
    QCOMPARE(before.size(), 8);

    // The checkout is derived state. Deleting it must be survivable, exactly
    // as deleting the SQLite projection will have to be in Increment 3.
    QVERIFY(QDir(store.repositoryPath() + QStringLiteral("/records")).removeRecursively());

    QVERIFY2(store.restoreFromDurableState(&error), qPrintable(error.message()));

    QCOMPARE(store.stateToken(), tokenBefore);
    const QList<MediaId> after = store.listIds(nullptr);
    QCOMPARE(after.size(), before.size());
    for (int index = 0; index < before.size(); ++index) {
        PIMIO_COMPARE_ID(after.at(index), before.at(index));
    }
    const auto record = store.load(MediaId(QStringLiteral("rec-3")), &error);
    QVERIFY2(record.has_value(), qPrintable(error.message()));
    QCOMPARE(record->metadata.caption, QStringLiteral("caption 3"));
}

void TestLoreFaults::corruptCheckoutFileIsRecoverable()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("rec-1"), QStringLiteral("intact")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Save"), &error).has_value(), qPrintable(error.message()));

    const QString path = store.repositoryPath()
                         + QStringLiteral("/records/re/rec-1.json");
    QVERIFY(QFileInfo::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("}{ not json");
    file.close();

    // Corruption is reported, never silently interpreted as an empty record.
    Error loadError;
    QVERIFY(!store.load(MediaId(QStringLiteral("rec-1")), &loadError).has_value());
    PIMIO_COMPARE_ENUM(loadError.code(), ErrorCode::CorruptData);

    QVERIFY2(store.restoreFromDurableState(&error), qPrintable(error.message()));
    const auto record = store.load(MediaId(QStringLiteral("rec-1")), &error);
    QVERIFY2(record.has_value(), qPrintable(error.message()));
    QCOMPARE(record->metadata.caption, QStringLiteral("intact"));
}

void TestLoreFaults::interruptedWriteMarkerIsRepairedOnOpen()
{
    PIMIO_SKIP_WITHOUT_LORE();

    // The rare kill that produces this state cannot be scheduled reliably, so
    // the residue itself is reproduced directly: a zero-length pending marker
    // is exactly what a process killed between creating and writing one leaves.
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    QString tokenBefore;
    {
        LoreDurableStore store(storePath);
        Error error;
        QVERIFY2(store.open(&error), qPrintable(error.message()));
        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("kept"), QStringLiteral("committed")),
                            &error));
        QVERIFY2(store.commit(QStringLiteral("Baseline"), &error).has_value(),
                 qPrintable(error.message()));
        tokenBefore = store.stateToken();
        store.close();
    }

    const QString group = storePath
                          + QStringLiteral("/repository/.lore/immutable/index/00");
    QVERIFY(QDir().mkpath(group));
    const QString marker = group + QStringLiteral("/level.pending");
    {
        QFile file(marker);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    LoreDurableStore damaged(storePath);
    QVERIFY(damaged.needsRepairAfterInterruptedWrite());

    Error error;
    QVERIFY2(damaged.open(&error), qPrintable(error.message()));
    QVERIFY2(damaged.repairedInterruptedWriteOnOpen(),
             "the recovery has to be reported, not performed silently");
    QVERIFY(!QFileInfo::exists(marker));

    // The repair removes a marker that records nothing, so committed state is
    // exactly what it was.
    QCOMPARE(damaged.stateToken(), tokenBefore);
    QCOMPARE(damaged.listIds(nullptr).size(), 1);
    QVERIFY(damaged.load(MediaId(QStringLiteral("kept")), nullptr).has_value());

    // A healthy store neither reports nor performs a repair.
    damaged.close();
    LoreDurableStore healthy(storePath);
    QVERIFY(!healthy.needsRepairAfterInterruptedWrite());
    QVERIFY2(healthy.open(&error), qPrintable(error.message()));
    QVERIFY(!healthy.repairedInterruptedWriteOnOpen());
    QVERIFY(!healthy.repairAfterInterruptedWrite(&error));
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::NotFound);
}

void TestLoreFaults::concurrentWriterNeverProducesAPartialResult()
{
    PIMIO_SKIP_WITHOUT_LORE();
    QVERIFY2(QFileInfo::exists(m_helperPath), qPrintable(m_helperPath));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("kept"), QStringLiteral("committed")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Baseline"), &error).has_value(),
             qPrintable(error.message()));

    // A second writer is refused rather than allowed to race. LORE 0.8.5 does
    // not serialise concurrent committers safely, so the contention has to be
    // resolved before it reaches LORE at all.
    {
        LoreDurableStore second(storePath);
        Error secondError;
        QVERIFY2(!second.open(&secondError), "a second writer must not be able to open the store");
        PIMIO_COMPARE_ENUM(secondError.code(), ErrorCode::Conflict);
    }

    // The same refusal has to hold across processes, not just within one.
    QProcess helper;
    helper.start(m_helperPath,
                 {QStringLiteral("commit"), storePath, QStringLiteral("5"),
                  QStringLiteral("other")});
    QVERIFY(helper.waitForStarted(30'000));

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("mine"), QStringLiteral("from the test")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Concurrent save"), &error).has_value(),
             qPrintable(error.message()));

    QVERIFY(helper.waitForFinished(180'000));
    const QString helperOutput = QString::fromLocal8Bit(helper.readAll());
    QVERIFY2(helper.exitCode() != 0, qPrintable(helperOutput));
    QVERIFY2(helperOutput.contains(QStringLiteral("Another process")), qPrintable(helperOutput));

    // Nothing the refused writer intended is visible, and this writer's work is.
    QVERIFY(store.load(MediaId(QStringLiteral("kept")), nullptr).has_value());
    QVERIFY(store.load(MediaId(QStringLiteral("mine")), nullptr).has_value());
    QVERIFY(!store.load(MediaId(QStringLiteral("other-0000")), nullptr).has_value());

    // The lock is released with the store, so the next writer gets in.
    store.close();
    QProcess after;
    after.start(m_helperPath,
                {QStringLiteral("commit"), storePath, QStringLiteral("1"),
                 QStringLiteral("after")});
    QVERIFY(after.waitForStarted(30'000));
    QVERIFY(after.waitForFinished(180'000));
    const QString afterOutput = QString::fromLocal8Bit(after.readAll());
    QVERIFY2(after.exitCode() == 0, qPrintable(afterOutput));

    LoreDurableStore reopened(storePath);
    QVERIFY2(reopened.open(&error), qPrintable(error.message()));
    const QList<MediaId> ids = reopened.listIds(nullptr);
    for (const MediaId &id : ids) {
        QVERIFY2(reopened.load(id, nullptr).has_value(), qPrintable(id.value()));
    }
    QVERIFY(reopened.load(MediaId(QStringLiteral("after-0000")), nullptr).has_value());
}

void TestLoreFaults::largeRecordSetReportsSizeAndLatency()
{
    PIMIO_SKIP_WITHOUT_LORE();

    // Published, not enforced. Thresholds become blocking only once stable
    // baselines exist; see pimio-v1-implementation.md, Increment 12.
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    constexpr int kRecordCount = 2000;
    QElapsedTimer timer;

    timer.start();
    for (int index = 0; index < kRecordCount; ++index) {
        QVERIFY(store.stage(makeLoreRecord(QStringLiteral("rec-%1").arg(index, 6, 10,
                                                                       QLatin1Char('0')),
                                           QStringLiteral("caption %1").arg(index)),
                            &error));
    }
    const qint64 stageMs = timer.elapsed();

    timer.restart();
    QVERIFY2(store.commit(QStringLiteral("Initial import"), &error).has_value(),
             qPrintable(error.message()));
    const qint64 firstCommitMs = timer.elapsed();

    // The incremental case that matters after the first import: one changed
    // record in a repository that already holds many.
    timer.restart();
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("rec-000001"), QStringLiteral("edited")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Single edit"), &error).has_value(),
             qPrintable(error.message()));
    const qint64 incrementalCommitMs = timer.elapsed();

    timer.restart();
    const QList<MediaId> ids = store.listIds(nullptr);
    const qint64 listMs = timer.elapsed();

    timer.restart();
    const QString token = store.stateToken();
    const qint64 tokenMs = timer.elapsed();

    QCOMPARE(ids.size(), kRecordCount);
    QVERIFY(!token.isEmpty());

    const qint64 repositoryBytes = directorySizeBytes(store.repositoryPath());
    qInfo("records=%d stage=%lld ms first-commit=%lld ms incremental-commit=%lld ms "
          "list=%lld ms state-token=%lld ms repository=%lld KiB",
          kRecordCount, static_cast<long long>(stageMs), static_cast<long long>(firstCommitMs),
          static_cast<long long>(incrementalCommitMs), static_cast<long long>(listMs),
          static_cast<long long>(tokenMs), static_cast<long long>(repositoryBytes / 1024));

    const auto edited = store.load(MediaId(QStringLiteral("rec-000001")), &error);
    QVERIFY2(edited.has_value(), qPrintable(error.message()));
    QCOMPARE(edited->metadata.caption, QStringLiteral("edited"));
}

QTEST_GUILESS_MAIN(TestLoreFaults)

#include "tst_lore_faults.moc"
