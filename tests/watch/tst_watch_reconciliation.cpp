#include "pimio/watch/reconcile_worker.h"
#include "pimio/watch/watch_service.h"

#include "pimio/core/types.h"
#include "pimio/projection/projection_database.h"
#include "pimio/scan/library_root.h"
#include "pimio/scan/media_hasher.h"
#include "pimio/scan/scanner.h"

#include "pimio/testing/fake_clock.h"
#include "pimio/testing/fake_metadata_reader.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/memory_file_system.h"

#include <QDateTime>
#include <QMap>
#include <QSignalSpy>
#include <QTest>

#include <atomic>

using namespace pimio;
using namespace pimio::watch;

namespace {

const QString kRoot = QStringLiteral("/library");
const QDateTime kT0 = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

class OverflowingAdapter final : public WatchAdapter
{
public:
    bool start(const QString &rootPath, core::Error *) override
    {
        m_active = true;
        WatchEvent event;
        event.kind = WatchEventKind::Overflow;
        event.path = rootPath;
        emit eventOccurred(event);
        return true;
    }

    void stop() override { m_active = false; }
    bool isWatching() const override { return m_active; }

private:
    bool m_active = false;
};

/// One reconcile pass, exactly as WatchService's job worker would perform it:
/// scan, then rebuild the projection from the store.
core::Error reconcileOnce(scan::Scanner &scanner, projection::ProjectionDatabase &projection,
                         const core::DurableStore &store)
{
    core::JobRecord job;
    job.kind = core::JobKind::ReconcileRoot;
    job.payload = makeRootJobPayload(scan::LibraryRoot{kRoot, false});
    std::atomic<bool> cancel{false};
    return runReconcileJob(job, cancel, scanner, &projection, store);
}

/// A normalized, MediaId-independent view of a store's contents: the set of
/// (relative path, content fingerprint) pairs it holds. Two independently
/// seeded scans assign unrelated random MediaIds even for identical files, so
/// this is what "the same index" is actually compared on.
QMap<QString, QString> contentView(const core::DurableStore &store)
{
    QMap<QString, QString> view;
    core::Error error;
    const QList<core::MediaId> ids = store.listIds(&error);
    Q_ASSERT(!error.isError());
    for (const core::MediaId &id : ids) {
        core::Error loadError;
        const auto record = store.load(id, &loadError);
        Q_ASSERT(record.has_value());
        view.insert(record->identity.absolutePath, record->fingerprint.cacheKey());
    }
    return view;
}

QMap<QString, QString> contentView(projection::ProjectionDatabase &db)
{
    QMap<QString, QString> view;
    core::Error error;
    const QList<core::MediaId> ids = db.listIds(&error);
    Q_ASSERT(!error.isError());
    for (const core::MediaId &id : ids) {
        core::Error loadError;
        const auto record = db.load(id, &loadError);
        Q_ASSERT(record.has_value());
        view.insert(record->identity.absolutePath, record->fingerprint.cacheKey());
    }
    return view;
}

} // namespace

#define VERIFY_RECONCILE_OK(call) \
    do { \
        const core::Error verifyReconcileError = (call); \
        QVERIFY2(!verifyReconcileError.isError(), qPrintable(verifyReconcileError.message())); \
    } while (false)

/// Proves the watch-triggered reconcile pipeline converges to the same index
/// as a clean scan: many small, debounced reconcile calls driven by a mix of
/// create, rename, burst, duplicate, and dropped/overflowing triggers must
/// leave the store in exactly the state a single full pass over the same
/// starting point and the same final disk contents would.
class TestWatchReconciliation : public QObject
{
    Q_OBJECT

private slots:
    void manyIncrementalReconcilesMatchOneCleanPass();
    void renameKeepsTheSameMediaIdAcrossReconciles();
    void duplicateReconcileCallsAreIdempotent();
    void reconcilingOnlyAtTheEndStillConvergesAfterDroppedTriggers();
    void startupOverflowEnqueuesAReconcile();
};

void TestWatchReconciliation::manyIncrementalReconcilesMatchOneCleanPass()
{
    // --- Path A: many small reconcile calls, as watch would trigger. ---
    testing::MemoryFileSystem fsA;
    testing::FakeMetadataReader readerA;
    testing::FakeClock clockA(kT0);
    testing::MemoryDurableStore storeA(clockA);
    projection::ProjectionDatabase projectionA;
    QVERIFY(projectionA.openInMemory(nullptr));
    scan::Scanner scannerA(&fsA, &readerA, &storeA);

    fsA.addDirectory(kRoot);
    fsA.addFile(kRoot + "/a.jpg", "content-a", kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerA, projectionA, storeA)); // initial scan

    // Burst: several files created together, one reconcile.
    fsA.addFile(kRoot + "/b.jpg", "content-b", kT0);
    fsA.addFile(kRoot + "/c.jpg", "content-c", kT0);
    fsA.addFile(kRoot + "/d.jpg", "content-d", kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerA, projectionA, storeA));

    // Rename: same content, new path.
    const QByteArray bContents = fsA.readAll(kRoot + "/b.jpg", nullptr);
    fsA.remove(kRoot + "/b.jpg", nullptr);
    fsA.addFile(kRoot + "/b-renamed.jpg", bContents, kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerA, projectionA, storeA));

    // Duplicate trigger: reconcile called again with nothing changed.
    VERIFY_RECONCILE_OK(reconcileOnce(scannerA, projectionA, storeA));

    // Delete.
    fsA.remove(kRoot + "/c.jpg", nullptr);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerA, projectionA, storeA));

    // --- Path B: identical starting point and identical final disk state,
    // reached directly and reconciled in one single pass. ---
    testing::MemoryFileSystem fsB;
    testing::FakeMetadataReader readerB;
    testing::FakeClock clockB(kT0);
    testing::MemoryDurableStore storeB(clockB);
    projection::ProjectionDatabase projectionB;
    QVERIFY(projectionB.openInMemory(nullptr));
    scan::Scanner scannerB(&fsB, &readerB, &storeB);

    fsB.addDirectory(kRoot);
    fsB.addFile(kRoot + "/a.jpg", "content-a", kT0);
    fsB.addFile(kRoot + "/b-renamed.jpg", "content-b", kT0);
    fsB.addFile(kRoot + "/d.jpg", "content-d", kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerB, projectionB, storeB));

    QCOMPARE(contentView(storeA), contentView(storeB));
    QCOMPARE(contentView(projectionA), contentView(projectionB));
}

void TestWatchReconciliation::renameKeepsTheSameMediaIdAcrossReconciles()
{
    testing::MemoryFileSystem fs;
    testing::FakeMetadataReader reader;
    testing::FakeClock clock(kT0);
    testing::MemoryDurableStore store(clock);
    projection::ProjectionDatabase projection;
    QVERIFY(projection.openInMemory(nullptr));
    scan::Scanner scanner(&fs, &reader, &store);

    fs.addDirectory(kRoot);
    fs.addFile(kRoot + "/original.jpg", "stable-content", kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scanner, projection, store));

    core::Error error;
    const QList<core::MediaId> beforeIds = store.listIds(&error);
    QCOMPARE(beforeIds.size(), 1);
    const core::MediaId originalId = beforeIds.first();

    const QByteArray contents = fs.readAll(kRoot + "/original.jpg", nullptr);
    fs.remove(kRoot + "/original.jpg", nullptr);
    fs.addFile(kRoot + "/renamed.jpg", contents, kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scanner, projection, store));

    const QList<core::MediaId> afterIds = store.listIds(&error);
    QCOMPARE(afterIds.size(), 1);
    QCOMPARE(afterIds.first(), originalId);

    const auto record = store.load(originalId, &error);
    QVERIFY(record.has_value());
    QCOMPARE(record->identity.absolutePath, kRoot + "/renamed.jpg");
}

void TestWatchReconciliation::duplicateReconcileCallsAreIdempotent()
{
    testing::MemoryFileSystem fs;
    testing::FakeMetadataReader reader;
    testing::FakeClock clock(kT0);
    testing::MemoryDurableStore store(clock);
    projection::ProjectionDatabase projection;
    QVERIFY(projection.openInMemory(nullptr));
    scan::Scanner scanner(&fs, &reader, &store);

    fs.addDirectory(kRoot);
    fs.addFile(kRoot + "/only.jpg", "content", kT0);

    VERIFY_RECONCILE_OK(reconcileOnce(scanner, projection, store));
    const QMap<QString, QString> afterFirst = contentView(store);

    // Several duplicate watch-triggered reconciles in a row (as coalesced
    // duplicate events would eventually still each become due individually
    // if a caller did not coalesce them) must not change anything further.
    for (int i = 0; i < 5; ++i) {
        VERIFY_RECONCILE_OK(reconcileOnce(scanner, projection, store));
    }

    QCOMPARE(contentView(store), afterFirst);
    core::Error error;
    QCOMPARE(store.listIds(&error).size(), 1);
}

void TestWatchReconciliation::reconcilingOnlyAtTheEndStillConvergesAfterDroppedTriggers()
{
    // Simulates a watcher that silently drops every intermediate
    // notification (no overflow signalled, nothing recoverable from the
    // channel itself) and is only ever recovered by the periodic
    // missed-event fallback calling reconcile once, long after the fact.
    testing::MemoryFileSystem fsDropped;
    testing::FakeMetadataReader readerDropped;
    testing::FakeClock clockDropped(kT0);
    testing::MemoryDurableStore storeDropped(clockDropped);
    projection::ProjectionDatabase projectionDropped;
    QVERIFY(projectionDropped.openInMemory(nullptr));
    scan::Scanner scannerDropped(&fsDropped, &readerDropped, &storeDropped);

    fsDropped.addDirectory(kRoot);
    fsDropped.addFile(kRoot + "/first.jpg", "one", kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerDropped, projectionDropped, storeDropped));

    // Every one of these changes would normally each trigger a watch event,
    // but none of them is reconciled until the very last line: the
    // equivalent of every intermediate notification being dropped.
    fsDropped.addFile(kRoot + "/second.jpg", "two", kT0);
    fsDropped.addFile(kRoot + "/third.jpg", "three", kT0);
    fsDropped.remove(kRoot + "/second.jpg", nullptr);
    fsDropped.addFile(kRoot + "/fourth.jpg", "four", kT0);

    VERIFY_RECONCILE_OK(reconcileOnce(scannerDropped, projectionDropped, storeDropped));

    // Reference: the same starting point and the same final disk state,
    // reached and reconciled directly.
    testing::MemoryFileSystem fsDirect;
    testing::FakeMetadataReader readerDirect;
    testing::FakeClock clockDirect(kT0);
    testing::MemoryDurableStore storeDirect(clockDirect);
    projection::ProjectionDatabase projectionDirect;
    QVERIFY(projectionDirect.openInMemory(nullptr));
    scan::Scanner scannerDirect(&fsDirect, &readerDirect, &storeDirect);

    fsDirect.addDirectory(kRoot);
    fsDirect.addFile(kRoot + "/first.jpg", "one", kT0);
    fsDirect.addFile(kRoot + "/third.jpg", "three", kT0);
    fsDirect.addFile(kRoot + "/fourth.jpg", "four", kT0);
    VERIFY_RECONCILE_OK(reconcileOnce(scannerDirect, projectionDirect, storeDirect));

    QCOMPARE(contentView(storeDropped), contentView(storeDirect));
}

void TestWatchReconciliation::startupOverflowEnqueuesAReconcile()
{
    projection::JobQueue queue;
    core::Error error;
    QVERIFY2(queue.openInMemory(&error), qPrintable(error.message()));

    OverflowingAdapter adapter;
    WatchService service(&adapter, &queue);
    service.setDebounceMs(0);
    QSignalSpy spy(&service, &WatchService::reconcileEnqueued);

    QVERIFY(service.start(scan::LibraryRoot{kRoot, false}, &error));
    QTRY_COMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(0).toString().isEmpty());
    QVERIFY(spy.at(0).at(1).toBool());
    QVERIFY(!spy.at(0).at(2).toBool());
    QCOMPARE(queue.pendingCount(&error), 1);
}

QTEST_GUILESS_MAIN(TestWatchReconciliation)

#include "tst_watch_reconciliation.moc"
