#include "tst_projection_metadata_fixture.h"

using namespace pimio;
using namespace pimio::projection;

void TestProjectionMetadata::metadataConflictsAreStoredAndReloaded()
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

void TestProjectionMetadata::gpsLocationSurvivesRebuild()
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

void TestProjectionMetadata::rotationIsPersisted()
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

void TestProjectionMetadata::timezoneAwareSortKey()
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

void TestProjectionMetadata::timezoneNaiveSortByWallClock()
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

void TestProjectionMetadata::unsupportedMediaStoredWithUnknownKind()
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

void TestProjectionMetadata::cameraAndDimensionFieldsSurviveRebuild()
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

void TestProjectionMetadata::videoDurationAndAudioFlagSurviveRebuild()
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

QTEST_MAIN(TestProjectionMetadata)
#include "tst_projection_metadata.moc"
