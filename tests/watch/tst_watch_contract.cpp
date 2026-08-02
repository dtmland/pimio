#include "pimio/watch/event_coalescer.h"

#include <QDateTime>
#include <QTest>

using namespace pimio::watch;

namespace {

const QDateTime kT0 = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

WatchEvent makeEvent(WatchEventKind kind, const QString &path, quint32 cookie = 0)
{
    WatchEvent event;
    event.kind = kind;
    event.path = path;
    event.renameCookie = cookie;
    return event;
}

} // namespace

/// Contract tests for EventCoalescer: the portable behaviour every
/// WatchAdapter's raw events are turned into, independent of which native
/// API produced them. Every scenario is driven with explicit QDateTime
/// values, never a real timer, so it is exactly as deterministic on every
/// platform runner as the durable-store and projection contract tests.
class TestWatchContract : public QObject
{
    Q_OBJECT

private slots:
    void aSingleCreateBecomesDueAfterTheDebounceWindow();
    void nothingIsDueBeforeTheDebounceWindowElapses();
    void aBurstOfManyEventsCollapsesToOneReconcile();
    void duplicateEventsForTheSamePathCollapseToOneReconcile();
    void renameHalvesInOrderArePaired();
    void renameHalvesOutOfOrderAreStillPaired();
    void anUnpairedRenameHalfStillTriggersReconcileAndExpires();
    void overflowTriggersReconcileAndClearsInFlightPairing();
    void takeDueReconcileIsFalseWhenNothingIsPending();
    void aSecondBatchAfterConsumptionGetsItsOwnDebounceWindow();
    void droppedEventsAreCaughtByThePeriodicFallbackAlone();
    void periodicFallbackIsNotDueBeforeItsInterval();
    void aFreshCoalescerWithNoPriorReconcileIsImmediatelyDueForFallback();
};

void TestWatchContract::aSingleCreateBecomesDueAfterTheDebounceWindow()
{
    EventCoalescer coalescer(/*debounceMs=*/500);
    coalescer.ingest(makeEvent(WatchEventKind::Created, "/lib/a.jpg"), kT0);

    QVERIFY(coalescer.hasPendingChange(kT0));
    QVERIFY(!coalescer.takeDueReconcile(kT0.addMSecs(100)));

    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(500)));
    QCOMPARE(coalescer.lastReconcileEventCount(), 1);
    QVERIFY(!coalescer.lastReconcileWasOverflow());
}

void TestWatchContract::nothingIsDueBeforeTheDebounceWindowElapses()
{
    EventCoalescer coalescer(/*debounceMs=*/1000);
    coalescer.ingest(makeEvent(WatchEventKind::Modified, "/lib/a.jpg"), kT0);

    QVERIFY(!coalescer.takeDueReconcile(kT0.addMSecs(999)));
    QVERIFY(coalescer.hasPendingChange(kT0.addMSecs(999)));
}

void TestWatchContract::aBurstOfManyEventsCollapsesToOneReconcile()
{
    EventCoalescer coalescer(/*debounceMs=*/500);
    for (int i = 0; i < 50; ++i) {
        coalescer.ingest(makeEvent(WatchEventKind::Created, QStringLiteral("/lib/burst-%1.jpg").arg(i)),
                         kT0.addMSecs(i));
    }

    QVERIFY(!coalescer.takeDueReconcile(kT0.addMSecs(200)));
    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(600)));
    QCOMPARE(coalescer.lastReconcileEventCount(), 50);

    // The batch was consumed: nothing else is due immediately afterwards.
    QVERIFY(!coalescer.takeDueReconcile(kT0.addMSecs(601)));
}

void TestWatchContract::duplicateEventsForTheSamePathCollapseToOneReconcile()
{
    EventCoalescer coalescer(/*debounceMs=*/300);
    for (int i = 0; i < 10; ++i) {
        coalescer.ingest(makeEvent(WatchEventKind::Modified, "/lib/same.jpg"), kT0.addMSecs(i * 5));
    }

    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(400)));
    // A reconcile is a full re-scan of the root regardless of how many
    // duplicate notifications preceded it, so exactly one reconcile is
    // produced for any number of duplicates, exactly like the burst case.
    QCOMPARE(coalescer.lastReconcileEventCount(), 10);
}

void TestWatchContract::renameHalvesInOrderArePaired()
{
    EventCoalescer coalescer(/*debounceMs=*/500);
    coalescer.ingest(makeEvent(WatchEventKind::RenamedFrom, "/lib/old.jpg", /*cookie=*/7), kT0);
    QCOMPARE(coalescer.pendingRenameHalfCount(), 1);
    QCOMPARE(coalescer.pairedRenameCount(), 0);

    coalescer.ingest(makeEvent(WatchEventKind::RenamedTo, "/lib/new.jpg", /*cookie=*/7),
                     kT0.addMSecs(10));
    QCOMPARE(coalescer.pendingRenameHalfCount(), 0);
    QCOMPARE(coalescer.pairedRenameCount(), 1);

    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(600)));
}

void TestWatchContract::renameHalvesOutOfOrderAreStillPaired()
{
    // Some native rename-cookie schemes may deliver the "to" half before the
    // "from" half under load. Pairing must not assume arrival order.
    EventCoalescer coalescer(/*debounceMs=*/500);
    coalescer.ingest(makeEvent(WatchEventKind::RenamedTo, "/lib/new.jpg", /*cookie=*/42), kT0);
    QCOMPARE(coalescer.pendingRenameHalfCount(), 1);

    coalescer.ingest(makeEvent(WatchEventKind::RenamedFrom, "/lib/old.jpg", /*cookie=*/42),
                     kT0.addMSecs(5));
    QCOMPARE(coalescer.pendingRenameHalfCount(), 0);
    QCOMPARE(coalescer.pairedRenameCount(), 1);
}

void TestWatchContract::anUnpairedRenameHalfStillTriggersReconcileAndExpires()
{
    // A file moved outside the watched tree only ever produces one half.
    // Scanner does not need rename semantics to reconcile correctly, so a
    // lone half must still mark the root pending...
    EventCoalescer coalescer(/*debounceMs=*/200, /*renamePairingTimeoutMs=*/1000);
    coalescer.ingest(makeEvent(WatchEventKind::RenamedFrom, "/lib/moved-away.jpg", /*cookie=*/9),
                     kT0);

    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(250)));
    QCOMPARE(coalescer.pendingRenameHalfCount(), 1);

    // ...and the unmatched half is eventually dropped from the pairing
    // table rather than lingering forever.
    coalescer.takeDueReconcile(kT0.addMSecs(1300));
    QCOMPARE(coalescer.pendingRenameHalfCount(), 0);
}

void TestWatchContract::overflowTriggersReconcileAndClearsInFlightPairing()
{
    EventCoalescer coalescer(/*debounceMs=*/200);
    coalescer.ingest(makeEvent(WatchEventKind::RenamedFrom, "/lib/old.jpg", /*cookie=*/3), kT0);
    QCOMPARE(coalescer.pendingRenameHalfCount(), 1);

    WatchEvent overflow;
    overflow.kind = WatchEventKind::Overflow;
    overflow.path = "/lib";
    coalescer.ingest(overflow, kT0.addMSecs(10));

    // An overflow may have swallowed the matching half; trusting a pairing
    // built from incomplete information risks pairing the wrong files.
    QCOMPARE(coalescer.pendingRenameHalfCount(), 0);

    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(300)));
    QVERIFY(coalescer.lastReconcileWasOverflow());
}

void TestWatchContract::takeDueReconcileIsFalseWhenNothingIsPending()
{
    EventCoalescer coalescer;
    QVERIFY(!coalescer.takeDueReconcile(kT0));
    QVERIFY(!coalescer.takeDueReconcile(kT0.addYears(1)));
}

void TestWatchContract::aSecondBatchAfterConsumptionGetsItsOwnDebounceWindow()
{
    EventCoalescer coalescer(/*debounceMs=*/500);
    coalescer.ingest(makeEvent(WatchEventKind::Created, "/lib/a.jpg"), kT0);
    QVERIFY(coalescer.takeDueReconcile(kT0.addMSecs(500)));

    // A change observed well after the first batch was consumed starts a
    // fresh debounce window rather than being immediately due.
    const QDateTime secondStart = kT0.addMSecs(10000);
    coalescer.ingest(makeEvent(WatchEventKind::Created, "/lib/b.jpg"), secondStart);
    QVERIFY(!coalescer.takeDueReconcile(secondStart.addMSecs(100)));
    QVERIFY(coalescer.takeDueReconcile(secondStart.addMSecs(500)));
    QCOMPARE(coalescer.lastReconcileEventCount(), 1);
}

void TestWatchContract::droppedEventsAreCaughtByThePeriodicFallbackAlone()
{
    // No event is ever ingested here at all: this is the scenario where the
    // watch channel drops a notification silently, with no overflow signal.
    // Only the interval-based fallback, which does not depend on having
    // observed anything, can recover from it.
    const QDateTime lastReconcileAt = kT0;
    QVERIFY(!EventCoalescer::isPeriodicFallbackDue(lastReconcileAt, kT0.addSecs(60),
                                                   /*intervalMs=*/900000));
    QVERIFY(EventCoalescer::isPeriodicFallbackDue(lastReconcileAt, kT0.addMSecs(900000),
                                                  /*intervalMs=*/900000));
    QVERIFY(EventCoalescer::isPeriodicFallbackDue(lastReconcileAt, kT0.addSecs(3600),
                                                  /*intervalMs=*/900000));
}

void TestWatchContract::periodicFallbackIsNotDueBeforeItsInterval()
{
    const QDateTime lastReconcileAt = kT0;
    QVERIFY(!EventCoalescer::isPeriodicFallbackDue(lastReconcileAt, kT0.addMSecs(1),
                                                   /*intervalMs=*/60000));
}

void TestWatchContract::aFreshCoalescerWithNoPriorReconcileIsImmediatelyDueForFallback()
{
    QVERIFY(EventCoalescer::isPeriodicFallbackDue(QDateTime(), kT0, /*intervalMs=*/900000));
}

QTEST_APPLESS_MAIN(TestWatchContract)

#include "tst_watch_contract.moc"
