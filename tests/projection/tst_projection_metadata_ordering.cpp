#include "tst_projection_metadata_fixture.h"

using namespace pimio;
using namespace pimio::projection;

void TestProjectionMetadata::captureTimeSortOrderIsChronological()
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
void TestProjectionMetadata::captureTimeEqualTimestampsOrderedById()
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

void TestProjectionMetadata::captureTimeMissingTimestampsSortFirstAndByIdAmongThemselves()
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

void TestProjectionMetadata::captureTimePaginationReturnsCorrectSlice()
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

void TestProjectionMetadata::filterByKindReturnsOnlyMatchingItems()
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

void TestProjectionMetadata::filterByMinimumRatingReturnsCorrectSubset()
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
