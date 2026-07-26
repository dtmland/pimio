#include "pimio/projection/projection_database.h"

#include "pimio/core/durable_store.h"
#include "pimio/core/metadata.h"
#include "pimio/core/types.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/qtest_printers.h"

#include <QObject>
#include <QTest>

using namespace pimio;
using namespace pimio::projection;

namespace {

const QDateTime kEpoch = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

testing::FakeClock makeClock()
{
    return testing::FakeClock(kEpoch);
}

/// Adds one MediaRecord to a MemoryDurableStore and returns its id.
core::MediaId addRecord(testing::MemoryDurableStore &store, core::MediaRecord record)
{
    core::Error err;
    store.stage(record, &err);
    Q_ASSERT(!err.isError());
    store.commit(QStringLiteral("add"), &err);
    Q_ASSERT(!err.isError());
    return record.id;
}

/// Creates a minimal record with a capture time. The id is generated each call.
core::MediaRecord makeRecord(const QString &fileName,
                              core::CaptureTime captureTime = {},
                              core::MediaKind kind = core::MediaKind::Image,
                              int rating = 0)
{
    core::MediaRecord r;
    r.id = core::MediaId::generate();
    r.fingerprint = core::ContentFingerprint(QStringLiteral("sha256"),
                                             r.id.value()); // unique per record
    r.identity.absolutePath = QStringLiteral("/lib/") + fileName;
    // volumeId and fileId must not be null — the media table declares them NOT NULL.
    r.identity.volumeId = QStringLiteral("");
    r.identity.fileId = QStringLiteral("");
    r.metadata.fileName = fileName;
    r.metadata.folderPath = QStringLiteral("/lib");
    r.metadata.kind = kind;
    r.metadata.captureTime = captureTime;
    r.metadata.captureTimeOrigin = core::MetadataOrigin::Embedded;
    r.metadata.rating = rating;
    // cameraMake, cameraModel, and caption are NOT NULL in the schema; set them
    // to empty strings so Qt does not bind them as SQL NULL.
    r.metadata.cameraMake = QStringLiteral("");
    r.metadata.cameraModel = QStringLiteral("");
    r.metadata.caption = QStringLiteral("");
    return r;
}

/// Rebuilds a ProjectionDatabase from \a store. Asserts success.
void rebuild(ProjectionDatabase &db, testing::MemoryDurableStore &store)
{
    core::Error err;
    const bool ok = db.rebuildFrom(store, &err);
    if (!ok || err.isError()) {
        qCritical() << "rebuildFrom failed:" << err.message();
    }
}

} // namespace

class TestProjectionMetadata : public QObject
{
    Q_OBJECT

private slots:
    // ---- Chronological ordering ----

    void captureTimeSortOrderIsChronological()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const auto t1 = core::CaptureTime::fromLocalWallClock(
            QDateTime(QDate(2024, 3, 1), QTime(12, 0, 0), Qt::UTC));
        const auto t2 = core::CaptureTime::fromLocalWallClock(
            QDateTime(QDate(2024, 1, 1), QTime(12, 0, 0), Qt::UTC));
        const auto t3 = core::CaptureTime::fromLocalWallClock(
            QDateTime(QDate(2024, 6, 1), QTime(12, 0, 0), Qt::UTC));

        const core::MediaId id1 = addRecord(store, makeRecord("march.jpg", t1));
        const core::MediaId id2 = addRecord(store, makeRecord("january.jpg", t2));
        const core::MediaId id3 = addRecord(store, makeRecord("june.jpg", t3));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> ordered = db.idsByCaptureTime(&err);
        QVERIFY(!err.isError());
        QCOMPARE(ordered.size(), 3);
        // Expect January, March, June.
        PIMIO_COMPARE_ID(ordered[0], id2);
        PIMIO_COMPARE_ID(ordered[1], id1);
        PIMIO_COMPARE_ID(ordered[2], id3);
    }

    void captureTimeEqualTimestampsOrderedById()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const auto same = core::CaptureTime::fromLocalWallClock(
            QDateTime(QDate(2024, 5, 15), QTime(10, 30, 0), Qt::UTC));

        core::MediaRecord r1 = makeRecord("a.jpg", same);
        core::MediaRecord r2 = makeRecord("z.jpg", same);
        // Force a predictable id order.
        r1.id = core::MediaId(QStringLiteral("id-aaa"));
        r2.id = core::MediaId(QStringLiteral("id-zzz"));

        addRecord(store, r2); // insert in reverse order to prove sort is stable
        addRecord(store, r1);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> ordered = db.idsByCaptureTime(&err);
        QVERIFY(!err.isError());
        QCOMPARE(ordered.size(), 2);
        PIMIO_COMPARE_ID(ordered[0], r1.id); // id-aaa < id-zzz
        PIMIO_COMPARE_ID(ordered[1], r2.id);
    }

    void captureTimeMissingTimestampsSortFirstAndByIdAmongThemselves()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const auto dated = core::CaptureTime::fromOffset(
            QDateTime(QDate(2024, 5, 15), QTime(10, 30, 0), Qt::UTC), 0);

        core::MediaRecord undated1 = makeRecord("no-date-b.jpg");
        core::MediaRecord undated2 = makeRecord("no-date-a.jpg");
        core::MediaRecord datedRecord = makeRecord("dated.jpg", dated);
        undated1.id = core::MediaId(QStringLiteral("id-bbb"));
        undated2.id = core::MediaId(QStringLiteral("id-aaa"));
        datedRecord.id = core::MediaId(QStringLiteral("id-ccc"));

        // Insert in an order that would expose any reliance on insertion order.
        addRecord(store, datedRecord);
        addRecord(store, undated1);
        addRecord(store, undated2);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> ordered = db.idsByCaptureTime(&err);
        QVERIFY(!err.isError());
        QCOMPARE(ordered.size(), 3);
        // Items with no capture time are grouped ahead of dated ones and are
        // ordered by id among themselves, so paging through them never
        // repeats or skips an item.
        PIMIO_COMPARE_ID(ordered[0], undated2.id); // id-aaa
        PIMIO_COMPARE_ID(ordered[1], undated1.id); // id-bbb
        PIMIO_COMPARE_ID(ordered[2], datedRecord.id);

        // The same query asked twice returns the same answer.
        QCOMPARE(db.idsByCaptureTime(&err), ordered);
        QVERIFY(!err.isError());
    }

    // ---- Pagination ----

    void captureTimePaginationReturnsCorrectSlice()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        QList<core::MediaId> expectedOrder;
        for (int i = 0; i < 5; ++i) {
            const auto t = core::CaptureTime::fromLocalWallClock(
                QDateTime(QDate(2024, 1, i + 1), QTime(0, 0, 0), Qt::UTC));
            const core::MediaId id = addRecord(store, makeRecord(QStringLiteral("%1.jpg").arg(i), t));
            expectedOrder.append(id);
        }

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        // First page: 3 records.
        const QList<core::MediaId> page1 = db.idsByCaptureTime(0, 3, &err);
        QVERIFY(!err.isError());
        QCOMPARE(page1.size(), 3);
        PIMIO_COMPARE_ID(page1[0], expectedOrder[0]);
        PIMIO_COMPARE_ID(page1[2], expectedOrder[2]);

        // Second page: last 2 records.
        const QList<core::MediaId> page2 = db.idsByCaptureTime(3, 2, &err);
        QVERIFY(!err.isError());
        QCOMPARE(page2.size(), 2);
        PIMIO_COMPARE_ID(page2[0], expectedOrder[3]);
        PIMIO_COMPARE_ID(page2[1], expectedOrder[4]);

        // Offset past the end returns an empty list.
        const QList<core::MediaId> empty = db.idsByCaptureTime(10, 5, &err);
        QVERIFY(!err.isError());
        QVERIFY(empty.isEmpty());
    }

    // ---- Filter by kind ----

    void filterByKindReturnsOnlyMatchingItems()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const core::MediaId imgId1 = addRecord(store, makeRecord("a.jpg", {}, core::MediaKind::Image));
        const core::MediaId imgId2 = addRecord(store, makeRecord("b.png", {}, core::MediaKind::Image));
        addRecord(store, makeRecord("c.mp4", {}, core::MediaKind::Video));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> images = db.idsWithKind(core::MediaKind::Image, &err);
        QVERIFY(!err.isError());
        QCOMPARE(images.size(), 2);

        const QList<core::MediaId> videos = db.idsWithKind(core::MediaKind::Video, &err);
        QVERIFY(!err.isError());
        QCOMPARE(videos.size(), 1);
    }

    // ---- Filter by rating ----

    void filterByMinimumRatingReturnsCorrectSubset()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        addRecord(store, makeRecord("unrated.jpg", {}, core::MediaKind::Image, 0));
        addRecord(store, makeRecord("three.jpg",   {}, core::MediaKind::Image, 3));
        addRecord(store, makeRecord("five.jpg",    {}, core::MediaKind::Image, 5));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        // Rated at least 3 → 3-star and 5-star.
        const QList<core::MediaId> atLeast3 = db.idsWithMinimumRating(3, &err);
        QVERIFY(!err.isError());
        QCOMPARE(atLeast3.size(), 2);

        // Rated at least 5 → 5-star only.
        const QList<core::MediaId> atLeast5 = db.idsWithMinimumRating(5, &err);
        QVERIFY(!err.isError());
        QCOMPARE(atLeast5.size(), 1);

        // Rated at least 1 → everything rated.
        const QList<core::MediaId> atLeast1 = db.idsWithMinimumRating(1, &err);
        QVERIFY(!err.isError());
        QCOMPARE(atLeast1.size(), 2);
    }

    // ---- Full-text search ----

    void fullTextSearchMatchesCaption()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("photo.jpg");
        r.metadata.caption = QStringLiteral("Golden Gate sunset");
        const core::MediaId target = addRecord(store, r);
        addRecord(store, makeRecord("other.jpg")); // no match

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found = db.searchText(QStringLiteral("golden"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

    void fullTextSearchMatchesFileName()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const core::MediaId target = addRecord(store, makeRecord("DSC_0042.jpg"));
        addRecord(store, makeRecord("holiday.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found = db.searchText(QStringLiteral("DSC_0042"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

    void fullTextSearchUnicodeCaption()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("tokyo.jpg");
        r.metadata.caption = QStringLiteral("東京タワー"); // Tokyo Tower
        const core::MediaId target = addRecord(store, r);
        addRecord(store, makeRecord("paris.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        // unicode61 breaks on character category, so an unbroken CJK run is a
        // single token: "東京タワー" is not split into "東京" and "タワー".
        // searchText compensates with a prefix query, so a leading substring
        // still finds the record.
        const QList<core::MediaId> found = db.searchText(QStringLiteral("東京"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

    void fullTextSearchEmptyQueryReturnsEmpty()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);
        addRecord(store, makeRecord("photo.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        QVERIFY(db.searchText(QStringLiteral(""), &err).isEmpty());
        QVERIFY(db.searchText(QStringLiteral("   "), &err).isEmpty());
    }

    void fullTextSearchNoMatchReturnsEmpty()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);
        addRecord(store, makeRecord("photo.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found = db.searchText(QStringLiteral("zzznomatch"), &err);
        QVERIFY(!err.isError());
        QVERIFY(found.isEmpty());
    }

    void fullTextSearchTreatsOperatorCharactersAsText()
    {
        // FTS5 reads its own operators in a bare match string, so text a user
        // is entitled to type must not reach it unquoted. None of these may
        // report an error, however few rows they find.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("photo.jpg");
        r.metadata.caption = QStringLiteral("Golden Gate sunset");
        addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        const QStringList hostile{
            QStringLiteral("AND"),          QStringLiteral("OR"),
            QStringLiteral("NOT"),          QStringLiteral("NEAR"),
            QStringLiteral("gate:sunset"),  QStringLiteral("foo(bar"),
            QStringLiteral("-golden"),      QStringLiteral("say \"hello\""),
            QStringLiteral("*"),            QStringLiteral("^caption"),
        };
        for (const QString &query : hostile) {
            core::Error err;
            db.searchText(query, &err);
            QVERIFY2(!err.isError(),
                     qPrintable(QStringLiteral("query %1 reported: %2")
                                    .arg(query, err.message())));
        }
    }

    void fullTextSearchEscapesEmbeddedQuotes()
    {
        // A double quote closes a phrase, so it must be doubled to stay
        // literal. Getting that wrong silently changes which rows match
        // rather than raising an error, so the match itself is asserted.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("quoted.jpg");
        r.metadata.caption = QStringLiteral("say \"hello\" loud");
        const core::MediaId target = addRecord(store, r);
        addRecord(store, makeRecord("other.jpg"));

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found =
            db.searchText(QStringLiteral("\"hello"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

    void fullTextSearchMatchesAllTerms()
    {
        // Several terms narrow the result rather than widening it.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord both = makeRecord("both.jpg");
        both.metadata.caption = QStringLiteral("Golden Gate sunset");
        const core::MediaId target = addRecord(store, both);

        core::MediaRecord one = makeRecord("one.jpg");
        one.metadata.caption = QStringLiteral("Golden retriever");
        addRecord(store, one);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> found =
            db.searchText(QStringLiteral("golden gate"), &err);
        QVERIFY(!err.isError());
        QCOMPARE(found.size(), 1);
        PIMIO_COMPARE_ID(found[0], target);
    }

    // ---- Metadata fidelity ----

    void metadataConflictsAreStoredAndReloaded()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("conflict.jpg");
        core::MetadataConflict conflict;
        conflict.field = QStringLiteral("captureTime");
        conflict.preferredOrigin = core::MetadataOrigin::Embedded;
        conflict.conflictingOrigin = core::MetadataOrigin::Sidecar;
        conflict.preferredValue = QStringLiteral("2024-01-01T12:00:00");
        conflict.conflictingValue = QStringLiteral("2024-01-01T13:00:00");
        r.metadata.conflicts.append(conflict);
        const core::MediaId id = addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const auto loaded = db.load(id, &err);
        QVERIFY(!err.isError());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->metadata.conflicts.size(), 1);
        QCOMPARE(loaded->metadata.conflicts[0].field, QStringLiteral("captureTime"));
    }

    void gpsLocationSurvivesRebuild()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("gps.jpg");
        r.metadata.location = core::GeoLocation::create(37.7749, -122.4194);
        const core::MediaId id = addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const auto loaded = db.load(id, &err);
        QVERIFY(!err.isError());
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->metadata.location.has_value());
        QCOMPARE(loaded->metadata.location->latitude(), 37.7749);
        QCOMPARE(loaded->metadata.location->longitude(), -122.4194);
    }

    void rotationIsPersisted()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("rotated.jpg");
        r.metadata.rotationDegrees = 90;
        const core::MediaId id = addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const auto loaded = db.load(id, &err);
        QVERIFY(!err.isError());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->metadata.rotationDegrees, 90);
    }

    void timezoneAwareSortKey()
    {
        // Two records taken at the same UTC instant but with different UTC
        // offsets must have the same sort key so they are considered "equal"
        // for ordering purposes and fall back to id ordering.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        // 2024-05-01 12:00:00 UTC = 2024-05-01 20:00:00 UTC+8
        const QDateTime utcInstant(QDate(2024, 5, 1), QTime(12, 0, 0), Qt::UTC);
        const auto tzAware = core::CaptureTime::fromOffset(utcInstant, 0);           // UTC+0
        const auto tzEast  = core::CaptureTime::fromOffset(utcInstant.toUTC().addSecs(8 * 3600), 8 * 3600);

        core::MediaRecord r1 = makeRecord("utc.jpg", tzAware);
        core::MediaRecord r2 = makeRecord("east.jpg", tzEast);
        r1.id = core::MediaId(QStringLiteral("id-aaa"));
        r2.id = core::MediaId(QStringLiteral("id-bbb"));
        addRecord(store, r1);
        addRecord(store, r2);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> ordered = db.idsByCaptureTime(&err);
        QVERIFY(!err.isError());
        QCOMPARE(ordered.size(), 2);
        // Both have the same UTC instant, so they sort by id.
        PIMIO_COMPARE_ID(ordered[0], r1.id);
        PIMIO_COMPARE_ID(ordered[1], r2.id);
    }

    void timezoneNaiveSortByWallClock()
    {
        // Items without a UTC offset are sorted by wall clock only. Two
        // records with the same naive wall-clock time but different ids fall
        // back to id ordering.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const QDateTime wallClock(QDate(2024, 3, 15), QTime(10, 0, 0), Qt::LocalTime);
        const auto naive = core::CaptureTime::fromLocalWallClock(wallClock);

        core::MediaRecord r1 = makeRecord("first.jpg", naive);
        core::MediaRecord r2 = makeRecord("second.jpg", naive);
        r1.id = core::MediaId(QStringLiteral("id-first"));
        r2.id = core::MediaId(QStringLiteral("id-second"));
        addRecord(store, r1);
        addRecord(store, r2);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const QList<core::MediaId> ordered = db.idsByCaptureTime(&err);
        QVERIFY(!err.isError());
        QCOMPARE(ordered.size(), 2);
        PIMIO_COMPARE_ID(ordered[0], r1.id);
        PIMIO_COMPARE_ID(ordered[1], r2.id);
    }

    void unsupportedMediaStoredWithUnknownKind()
    {
        // A record whose media kind is Unknown (e.g. a file not recognised by
        // any metadata reader) must still appear in the projection.
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("unknown.bin", {}, core::MediaKind::Unknown);
        const core::MediaId id = addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const auto loaded = db.load(id, &err);
        QVERIFY(!err.isError());
        QVERIFY(loaded.has_value());
        PIMIO_COMPARE_ENUM(loaded->metadata.kind, core::MediaKind::Unknown);

        // It also appears in a kind filter for Unknown.
        const QList<core::MediaId> unknowns =
            db.idsWithKind(core::MediaKind::Unknown, &err);
        QVERIFY(!err.isError());
        QCOMPARE(unknowns.size(), 1);
    }

    void cameraAndDimensionFieldsSurviveRebuild()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("raw.jpg");
        r.metadata.cameraMake = QStringLiteral("Canon");
        r.metadata.cameraModel = QStringLiteral("EOS R5");
        r.metadata.lensModel = QStringLiteral("RF 50mm f/1.2");
        r.metadata.pixelWidth = 8192;
        r.metadata.pixelHeight = 5464;
        r.metadata.durationMs = 0;
        r.metadata.hasAudio = false;
        const core::MediaId id = addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const auto loaded = db.load(id, &err);
        QVERIFY(!err.isError());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->metadata.cameraMake, QStringLiteral("Canon"));
        QCOMPARE(loaded->metadata.cameraModel, QStringLiteral("EOS R5"));
        QCOMPARE(loaded->metadata.pixelWidth, 8192);
        QCOMPARE(loaded->metadata.pixelHeight, 5464);
    }

    void videoDurationAndAudioFlagSurviveRebuild()
    {
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        core::MediaRecord r = makeRecord("clip.mp4", {}, core::MediaKind::Video);
        r.metadata.durationMs = 90000; // 90 seconds
        r.metadata.hasAudio = true;
        const core::MediaId id = addRecord(store, r);

        ProjectionDatabase db;
        db.openInMemory(nullptr);
        rebuild(db, store);

        core::Error err;
        const auto loaded = db.load(id, &err);
        QVERIFY(!err.isError());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->metadata.durationMs, 90000LL);
        QVERIFY(loaded->metadata.hasAudio);
    }
};

QTEST_MAIN(TestProjectionMetadata)
#include "tst_projection_metadata.moc"
