#include "pimio/projection/projection_database.h"

#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/qtest_printers.h"
#include "pimio/testing/fake_clock.h"

#include <QFile>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <algorithm>
#include <QTest>

using namespace pimio::core;
using namespace pimio::projection;
using namespace pimio::testing;

namespace {

MediaRecord makeRecord(const QString &id, const QString &caption, const QString &digest,
                       qint64 captureMSecs, const QStringList &tags = {})
{
    MediaRecord record;
    record.id = MediaId(id);
    record.fingerprint = ContentFingerprint(QStringLiteral("blake3"), digest);
    record.identity.absolutePath = QStringLiteral("/library/%1.jpg").arg(id);
    record.identity.volumeId = QStringLiteral("vol-1");
    record.identity.fileId = id;
    record.identity.sizeBytes = 4096;
    record.identity.lastModified = QDateTime::fromMSecsSinceEpoch(captureMSecs, Qt::UTC);

    record.metadata.kind = MediaKind::Image;
    record.metadata.fileName = id + QStringLiteral(".jpg");
    record.metadata.folderPath = QStringLiteral("/library");
    record.metadata.captureTime = CaptureTime::fromOffset(
        QDateTime::fromMSecsSinceEpoch(captureMSecs, Qt::UTC), 0);
    record.metadata.captureTimeOrigin = MetadataOrigin::Embedded;
    record.metadata.cameraMake = QStringLiteral("Acme");
    record.metadata.cameraModel = QStringLiteral("Model 1");
    record.metadata.pixelWidth = 4000;
    record.metadata.pixelHeight = 3000;
    record.metadata.rating = 3;
    record.metadata.caption = caption;
    record.metadata.tags = tags;
    record.metadata.location = GeoLocation::create(51.5, -0.12);
    return record;
}

QList<QString> idValues(const QList<MediaId> &ids)
{
    QList<QString> values;
    values.reserve(ids.size());
    for (const MediaId &id : ids) {
        values.append(id.value());
    }
    return values;
}

} // namespace

class TestProjectionRebuild : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void anEmptyProjectionOpensAtTheCurrentSchemaVersion();
    void rebuildReproducesTheDurableStore();
    void deletingTheProjectionReproducesIdenticalQueryResults();
    void aProjectionIsStaleUntilItIsRebuiltForTheCurrentState();
    void aFailedRebuildLeavesThePreviousContentsIntact();
    void aCorruptProjectionIsReportedAndIsRecoverableByDeletingIt();
    void queriesMatchTheDurableStore();
    void applyRecordsAddsAndReplacesRowsWithoutClaimingToBeUpToDate();

private:
    void populate(MemoryDurableStore &store);
};

void TestProjectionRebuild::initTestCase()
{
    QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
             "the Qt SQLite driver must be present");
}

void TestProjectionRebuild::populate(MemoryDurableStore &store)
{
    Error error;
    QVERIFY(store.stage(makeRecord(QStringLiteral("alpha"), QStringLiteral("first"),
                                   QStringLiteral("aaa"), 1'000,
                                   {QStringLiteral("holiday"), QStringLiteral("beach")}),
                        &error));
    QVERIFY(store.stage(makeRecord(QStringLiteral("bravo"), QStringLiteral("second"),
                                   QStringLiteral("bbb"), 3'000, {QStringLiteral("holiday")}),
                        &error));
    // Same fingerprint as bravo: the duplicate case the projection must find.
    QVERIFY(store.stage(makeRecord(QStringLiteral("charlie"), QStringLiteral("third"),
                                   QStringLiteral("bbb"), 2'000),
                        &error));
    QVERIFY(store.commit(QStringLiteral("Initial import"), &error).has_value());
}

void TestProjectionRebuild::anEmptyProjectionOpensAtTheCurrentSchemaVersion()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("projection.db"));

    ProjectionDatabase projection;
    Error error;
    QVERIFY2(projection.open(path, &error), qPrintable(error.message()));
    QCOMPARE(projection.schemaVersion(), MigrationRunner(projectionMigrations()).latestVersion());
    QCOMPARE(projection.recordCount(&error), 0);
    QVERIFY(projection.projectedStateToken(&error).isEmpty());

    const int version = projection.schemaVersion();
    projection.close();
    QVERIFY(QFile::exists(path));
    QCOMPARE(projection.schemaVersion(), 0);

    // Reopening an already current projection must not rebuild or migrate it.
    ProjectionDatabase reopened;
    QVERIFY2(reopened.open(path, &error), qPrintable(error.message()));
    QCOMPARE(reopened.schemaVersion(), version);
}

void TestProjectionRebuild::rebuildReproducesTheDurableStore()
{
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    ProjectionDatabase projection;
    Error error;
    QVERIFY2(projection.openInMemory(&error), qPrintable(error.message()));
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));

    QCOMPARE(projection.recordCount(&error), 3);
    QCOMPARE(projection.projectedStateToken(&error), store.stateToken());

    QList<MediaId> storeIds = store.listIds(&error);
    std::sort(storeIds.begin(), storeIds.end(), [](const MediaId &a, const MediaId &b) {
        return a.value() < b.value();
    });
    QCOMPARE(idValues(projection.listIds(&error)), idValues(storeIds));

    // A projected record must be byte-identical to the durable one, not merely
    // similar: the projection is a copy, not an interpretation.
    for (const MediaId &id : storeIds) {
        const std::optional<MediaRecord> durable = store.load(id, &error);
        const std::optional<MediaRecord> projected = projection.load(id, &error);
        QVERIFY2(projected.has_value(), qPrintable(id.value()));
        QVERIFY2(durable.has_value(), qPrintable(id.value()));
        QCOMPARE(QJsonDocument(projected->toJson()).toJson(QJsonDocument::Compact),
                 QJsonDocument(durable->toJson()).toJson(QJsonDocument::Compact));
    }
}

void TestProjectionRebuild::deletingTheProjectionReproducesIdenticalQueryResults()
{
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("projection.db"));

    QList<QString> before;
    QList<QString> beforeByCapture;
    {
        ProjectionDatabase projection;
        Error error;
        QVERIFY2(projection.open(path, &error), qPrintable(error.message()));
        QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
        before = idValues(projection.listIds(&error));
        beforeByCapture = idValues(projection.idsByCaptureTime(&error));
    }

    // The requirement the whole design rests on: the cache is disposable.
    Error removeError;
    QVERIFY2(ProjectionDatabase::remove(path, &removeError), qPrintable(removeError.message()));
    QVERIFY(!QFile::exists(path));
    QVERIFY(!QFile::exists(path + QStringLiteral("-wal")));

    ProjectionDatabase rebuilt;
    Error error;
    QVERIFY2(rebuilt.open(path, &error), qPrintable(error.message()));
    QVERIFY(rebuilt.isStale(store, &error));
    QVERIFY2(rebuilt.rebuildFrom(store, &error), qPrintable(error.message()));

    QCOMPARE(idValues(rebuilt.listIds(&error)), before);
    QCOMPARE(idValues(rebuilt.idsByCaptureTime(&error)), beforeByCapture);
    QVERIFY(!rebuilt.isStale(store, &error));
}

void TestProjectionRebuild::aProjectionIsStaleUntilItIsRebuiltForTheCurrentState()
{
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    ProjectionDatabase projection;
    Error error;
    QVERIFY2(projection.openInMemory(&error), qPrintable(error.message()));

    // Never populated: stale, because it holds nothing for any state.
    QVERIFY(projection.isStale(store, &error));
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    QVERIFY(!projection.isStale(store, &error));

    // Staged work does not change committed state, so it does not make the
    // projection stale.
    QVERIFY(store.stage(makeRecord(QStringLiteral("delta"), QStringLiteral("fourth"),
                                   QStringLiteral("ddd"), 4'000),
                        &error));
    QVERIFY(!projection.isStale(store, &error));

    QVERIFY(store.commit(QStringLiteral("Add delta"), &error).has_value());
    QVERIFY(projection.isStale(store, &error));
    QCOMPARE(projection.recordCount(&error), 3);

    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    QVERIFY(!projection.isStale(store, &error));
    QCOMPARE(projection.recordCount(&error), 4);

    // A change made outside pimio must be detected too; that is what the state
    // token exists for.
    store.applyExternalChange(makeRecord(QStringLiteral("echo"), QStringLiteral("external"),
                                         QStringLiteral("eee"), 5'000));
    QVERIFY(projection.isStale(store, &error));
}

void TestProjectionRebuild::aFailedRebuildLeavesThePreviousContentsIntact()
{
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    ProjectionDatabase projection;
    Error error;
    QVERIFY2(projection.openInMemory(&error), qPrintable(error.message()));
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    const QString goodToken = projection.projectedStateToken(&error);
    const QList<QString> goodIds = idValues(projection.listIds(&error));

    // The store goes away mid-life, as an unmounted volume would.
    store.setAvailable(false);
    Error rebuildError;
    QVERIFY(!projection.rebuildFrom(store, &rebuildError));
    PIMIO_COMPARE_ENUM(rebuildError.code(), ErrorCode::StorageUnavailable);

    // A half-built index would be worse than a stale one, so the previous
    // contents must still be there and still be labelled with their own token.
    QCOMPARE(idValues(projection.listIds(&error)), goodIds);
    QCOMPARE(projection.projectedStateToken(&error), goodToken);

    store.setAvailable(true);
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    QCOMPARE(idValues(projection.listIds(&error)), goodIds);
}

void TestProjectionRebuild::aCorruptProjectionIsReportedAndIsRecoverableByDeletingIt()
{
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("projection.db"));

    {
        ProjectionDatabase projection;
        Error error;
        QVERIFY2(projection.open(path, &error), qPrintable(error.message()));
        QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    }

    // Overwrite the header. A file that is not a database must be reported as
    // damaged rather than silently replaced, because silently replacing a file
    // is how real data gets destroyed by a bug in the detection.
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(0));
        QVERIFY(file.write(QByteArray(64, '\x7f')) == 64);
    }

    ProjectionDatabase damaged;
    Error error;
    QVERIFY(!damaged.open(path, &error));
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::CorruptData);

    // Recovery is deleting the cache and rebuilding it, and it loses nothing.
    QVERIFY2(ProjectionDatabase::remove(path, &error), qPrintable(error.message()));
    ProjectionDatabase rebuilt;
    QVERIFY2(rebuilt.open(path, &error), qPrintable(error.message()));
    QVERIFY2(rebuilt.rebuildFrom(store, &error), qPrintable(error.message()));
    QCOMPARE(rebuilt.recordCount(&error), 3);
    QCOMPARE(rebuilt.projectedStateToken(&error), store.stateToken());
}

void TestProjectionRebuild::queriesMatchTheDurableStore()
{
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    ProjectionDatabase projection;
    Error error;
    QVERIFY2(projection.openInMemory(&error), qPrintable(error.message()));
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));

    QCOMPARE(idValues(projection.idsWithFingerprint(
                 ContentFingerprint(QStringLiteral("blake3"), QStringLiteral("bbb")), &error)),
             (QList<QString>{QStringLiteral("bravo"), QStringLiteral("charlie")}));
    QCOMPARE(idValues(projection.idsWithFingerprint(
                 ContentFingerprint(QStringLiteral("blake3"), QStringLiteral("zzz")), &error)),
             QList<QString>{});

    QCOMPARE(idValues(projection.idsWithTag(QStringLiteral("holiday"), &error)),
             (QList<QString>{QStringLiteral("alpha"), QStringLiteral("bravo")}));
    QCOMPARE(idValues(projection.idsWithTag(QStringLiteral("beach"), &error)),
             QList<QString>{QStringLiteral("alpha")});

    // Capture order, not insertion or id order.
    QCOMPARE(idValues(projection.idsByCaptureTime(&error)),
             (QList<QString>{QStringLiteral("alpha"), QStringLiteral("charlie"),
                             QStringLiteral("bravo")}));

    Error missing;
    QVERIFY(!projection.load(MediaId(QStringLiteral("nope")), &missing).has_value());
    PIMIO_COMPARE_ENUM(missing.code(), ErrorCode::NotFound);
}

void TestProjectionRebuild::applyRecordsAddsAndReplacesRowsWithoutClaimingToBeUpToDate()
{
    // A scan in progress projects each committed batch so the grid can show
    // it, without pretending the projection covers the whole store yet.
    FakeClock clock(QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC));
    MemoryDurableStore store(clock);
    populate(store);

    ProjectionDatabase projection;
    Error error;
    QVERIFY2(projection.openInMemory(&error), qPrintable(error.message()));
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    QCOMPARE(projection.recordCount(&error), 3);
    const QString tokenBefore = projection.projectedStateToken(&error);

    const MediaRecord fresh = makeRecord(QStringLiteral("delta"), QStringLiteral("fourth"),
                                         QStringLiteral("ddd"), 4'000,
                                         {QStringLiteral("new-tag")});
    const MediaRecord replacement = makeRecord(QStringLiteral("alpha"),
                                               QStringLiteral("rewritten"),
                                               QStringLiteral("aaa"), 1'000);
    QVERIFY2(projection.applyRecords({fresh, replacement}, &error), qPrintable(error.message()));

    QCOMPARE(projection.recordCount(&error), 4);
    QCOMPARE(projection.load(MediaId(QStringLiteral("alpha")), &error)->metadata.caption,
             QStringLiteral("rewritten"));
    QCOMPARE(projection.load(MediaId(QStringLiteral("delta")), &error)->metadata.caption,
             QStringLiteral("fourth"));
    QCOMPARE(idValues(projection.idsWithTag(QStringLiteral("new-tag"), &error)),
             QList<QString>({QStringLiteral("delta")}));
    // No duplicate rows for a record applied twice.
    QVERIFY2(projection.applyRecords({replacement}, &error), qPrintable(error.message()));
    QCOMPARE(projection.recordCount(&error), 4);

    // The state token is untouched, so isStale() still tells the truth: only a
    // full rebuild can vouch for the projection matching the store.
    QCOMPARE(projection.projectedStateToken(&error), tokenBefore);
}

QTEST_MAIN(TestProjectionRebuild)

#include "tst_projection_rebuild.moc"
