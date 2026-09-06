#include "tst_scan_incremental_fixture.h"

#include "scan_test_support.h"

#include "pimio/scan/media_hasher.h"
#include "pimio/scan/scanner.h"

#include "pimio/core/types.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/fake_metadata_reader.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/memory_file_system.h"
#include "pimio/testing/qtest_printers.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QTest>

using namespace pimio;
using namespace pimio::scan;
using pimio::tests::scan_support::addFile;
using pimio::tests::scan_support::kRoot;
using pimio::tests::scan_support::kT0;
using pimio::tests::scan_support::loadAll;
using pimio::tests::scan_support::makeClock;

void TestScanIncremental::scanAddsNewFiles()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/photo.jpg");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan({kRoot}, cancel, &result);

        QVERIFY(!err.isError());
        QCOMPARE(result.added, 1);
        QCOMPARE(result.unchanged, 0);
        QCOMPARE(result.updated, 0);
        QCOMPARE(result.removed, 0);
        QVERIFY(result.warnings.isEmpty());

        const QList<core::MediaRecord> records = loadAll(store);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].identity.absolutePath, kRoot + "/photo.jpg");
        QVERIFY(records[0].id.isValid());
        QVERIFY(records[0].fingerprint.isValid());
        QCOMPARE(records[0].metadata.fileName, QStringLiteral("photo.jpg"));
        QCOMPARE(records[0].originalStorage, core::MediaRecord::OriginalStorage::Managed);
        QVERIFY(records[0].managedOriginalPath.startsWith(QStringLiteral("originals/")));
    }

void TestScanIncremental::failedScanCommitRetainsManagedImportForRetry()
{
    testing::MemoryFileSystem fs;
    testing::FakeMetadataReader reader;
    auto clock = makeClock();
    testing::MemoryDurableStore store(clock);

    fs.addDirectory(kRoot);
    addFile(fs, kRoot + "/photo.jpg");
    store.failNextCommit(core::ErrorCode::OutOfSpace);

    Scanner scanner(&fs, &reader, &store);
    std::atomic<bool> cancel{false};
    Scanner::Result result;
    const core::Error scanError = scanner.scan({kRoot}, cancel, &result);

    QVERIFY(scanError.code() == core::ErrorCode::OutOfSpace);
    QVERIFY(store.hasStagedChanges());
    QVERIFY(loadAll(store).isEmpty());

    core::Error retryError;
    QVERIFY2(store.commit(QStringLiteral("Retry managed import"), &retryError).has_value(),
             qPrintable(retryError.message()));
    QCOMPARE(loadAll(store).size(), 1);
    QCOMPARE(loadAll(store).constFirst().originalStorage,
             core::MediaRecord::OriginalStorage::Managed);
}

void TestScanIncremental::batchedScanCommitsBeforeItFinishes()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        for (int i = 0; i < 10; ++i) {
            addFile(fs, kRoot + QStringLiteral("/photo%1.jpg").arg(i, 2, 10, QLatin1Char('0')),
                    QByteArray("content-") + QByteArray::number(i));
        }

        Scanner scanner(&fs, &reader, &store);
        scanner.setCommitBatchSize(4);
        QCOMPARE(scanner.commitBatchSize(), 4);

        // What a browser would see: how many records were readable from the
        // store at the moment each batch was reported.
        QList<int> visibleAtEachBatch;
        QList<int> batchSizes;
        QList<int> indexedAtEachBatch;
        const Scanner::ProgressCallback onProgress =
            [&](const QList<core::MediaRecord> &committed, const Scanner::Result &progress) {
                batchSizes.append(static_cast<int>(committed.size()));
                visibleAtEachBatch.append(static_cast<int>(loadAll(store).size()));
                indexedAtEachBatch.append(progress.added + progress.updated + progress.unchanged);
            };

        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan({kRoot}, cancel, &result, onProgress);

        QVERIFY(!err.isError());
        QCOMPARE(result.added, 10);
        QCOMPARE(loadAll(store).size(), 10);

        // Three reports: two full batches of four and the remaining two.
        QCOMPARE(batchSizes, QList<int>({4, 4, 2}));
        QCOMPARE(visibleAtEachBatch, QList<int>({4, 8, 10}));
        QCOMPARE(indexedAtEachBatch, QList<int>({4, 8, 10}));
    }

void TestScanIncremental::anUnbatchedScanCommitsOnceAtTheEnd()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        for (int i = 0; i < 5; ++i) {
            addFile(fs, kRoot + QStringLiteral("/photo%1.jpg").arg(i),
                    QByteArray("content-") + QByteArray::number(i));
        }

        Scanner scanner(&fs, &reader, &store);
        QCOMPARE(scanner.commitBatchSize(), 0);

        int reports = 0;
        int reportedRecords = 0;
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan(
            {kRoot}, cancel, &result,
            [&](const QList<core::MediaRecord> &committed, const Scanner::Result &) {
                ++reports;
                reportedRecords += static_cast<int>(committed.size());
            });

        QVERIFY(!err.isError());
        QCOMPARE(result.added, 5);
        QCOMPARE(reports, 1);
        QCOMPARE(reportedRecords, 5);
        QCOMPARE(loadAll(store).size(), 5);
    }

void TestScanIncremental::aCancelledBatchedScanKeepsWhatItAlreadyCommitted()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        for (int i = 0; i < 12; ++i) {
            addFile(fs, kRoot + QStringLiteral("/photo%1.jpg").arg(i, 2, 10, QLatin1Char('0')),
                    QByteArray("content-") + QByteArray::number(i));
        }

        Scanner scanner(&fs, &reader, &store);
        scanner.setCommitBatchSize(4);

        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan(
            {kRoot}, cancel, &result,
            [&cancel](const QList<core::MediaRecord> &, const Scanner::Result &) {
                // Stop the scan as soon as the first batch is durable.
                cancel.store(true);
            });

        QVERIFY(err.code() == core::ErrorCode::Cancelled);
        // The committed batch describes files that really are on disk, and a
        // later scan converges on the rest, so it is kept rather than undone.
        QCOMPARE(loadAll(store).size(), 4);

        // The next run finishes the job.
        cancel.store(false);
        Scanner::Result second;
        QVERIFY(!scanner.scan({kRoot}, cancel, &second).isError());
        QCOMPARE(loadAll(store).size(), 12);
    }

void TestScanIncremental::scanSkipsNonMediaFilesLikeDsStore()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/photo.jpg");
        // OS bookkeeping files have no media extension and no readable media
        // signature; they must not become thumbnail-less "gray square" records.
        fs.addFile(kRoot + "/.DS_Store", "\x00\x00\x00\x01Bud1");
        fs.addFile(kRoot + "/Thumbs.db", "not media");
        fs.addFile(kRoot + "/notes.txt", "just text");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan({kRoot}, cancel, &result);

        QVERIFY(!err.isError());
        QCOMPARE(result.added, 1); // only photo.jpg

        const QList<core::MediaRecord> records = loadAll(store);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].identity.absolutePath, kRoot + "/photo.jpg");
    }

void TestScanIncremental::scanRemovesAPreviouslyIndexedFileThatIsNoLongerMedia()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/photo.jpg");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result first;
        QVERIFY(!scanner.scan({kRoot}, cancel, &first).isError());
        QCOMPARE(first.added, 1);

        // The photo is replaced by a non-media file (e.g. the user renamed it
        // to an unrecognised extension). It must drop out of the library.
        QVERIFY(fs.remove(kRoot + "/photo.jpg", nullptr));
        fs.addFile(kRoot + "/photo.bin", "no longer an image");

        Scanner::Result second;
        QVERIFY(!scanner.scan({kRoot}, cancel, &second).isError());
        QCOMPARE(second.removed, 0);
        QCOMPARE(loadAll(store).size(), 1);
    }

void TestScanIncremental::repeatedUnchangedScanMakesNoUpdates()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/photo.jpg");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};

        Scanner::Result first;
        scanner.scan({kRoot}, cancel, &first);
        QCOMPARE(first.added, 1);

        Scanner::Result second;
        const core::Error err = scanner.scan({kRoot}, cancel, &second);

        QVERIFY(!err.isError());
        QCOMPARE(second.added, 0);
        QCOMPARE(second.updated, 0);
        QCOMPARE(second.removed, 0);
        QCOMPARE(second.unchanged, 1);
        QCOMPARE(loadAll(store).size(), 1);
    }

void TestScanIncremental::scanRemovesHistoricalSamePathDuplicates()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        const QString path = addFile(fs, kRoot + "/photo.jpg");
        core::Error identityError;
        const core::FileIdentity identity = fs.identify(path, &identityError);
        QVERIFY(!identityError.isError());

        for (const QString &id : {QStringLiteral("duplicate-a"), QStringLiteral("duplicate-b")}) {
            core::MediaRecord record;
            record.id = core::MediaId(id);
            record.identity = identity;
            record.fingerprint = MediaHasher::computeFingerprint("test-content");
            record.metadata.kind = core::MediaKind::Image;
            QVERIFY(store.stage(record, nullptr));
        }
        QVERIFY(store.commit(QStringLiteral("historical duplicates"), nullptr).has_value());
        QCOMPARE(loadAll(store).size(), 2);

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error error = scanner.scan({kRoot}, cancel, &result);

        QVERIFY(!error.isError());
        QCOMPARE(result.updated, 1);
        QCOMPARE(result.removed, 1);
        QCOMPARE(loadAll(store).size(), 1);
        QCOMPARE(loadAll(store).constFirst().identity.absolutePath, path);
    }

void TestScanIncremental::scanUpdatesChangedFile()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        const QString path = kRoot + "/photo.jpg";
        addFile(fs, path, "version-1", kT0);

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};

        scanner.scan({kRoot}, cancel);
        const core::MediaId originalId = loadAll(store)[0].id;
        const core::ContentFingerprint fp1 = loadAll(store)[0].fingerprint;
        const QString managedPath1 = loadAll(store)[0].managedOriginalPath;

        // Replace with different content.
        addFile(fs, path, "version-2", kT0.addSecs(60));

        Scanner::Result result;
        scanner.scan({kRoot}, cancel, &result);

        QCOMPARE(result.updated, 1);
        QCOMPARE(result.added, 0);
        QCOMPARE(result.removed, 0);

        const core::MediaRecord updated = loadAll(store)[0];
        QCOMPARE(updated.id, originalId); // stable identity
        QVERIFY(updated.fingerprint != fp1); // fingerprint changed
        QVERIFY(updated.managedOriginalPath != managedPath1);
    }

void TestScanIncremental::scanReadFailureKeepsManagedOriginal()
{
    testing::MemoryFileSystem fs;
    testing::FakeMetadataReader reader;
    auto clock = makeClock();
    testing::MemoryDurableStore store(clock);

    fs.addDirectory(kRoot);
    const QString path = addFile(fs, kRoot + "/photo.jpg", "version-1", kT0);
    Scanner scanner(&fs, &reader, &store);
    std::atomic<bool> cancel{false};
    QVERIFY(!scanner.scan({kRoot}, cancel).isError());
    const core::MediaRecord original = loadAll(store).constFirst();

    addFile(fs, path, "version-2", kT0.addSecs(60));
    fs.injectFailure(path, core::ErrorCode::PermissionDenied);
    Scanner::Result result;
    QVERIFY(!scanner.scan({kRoot}, cancel, &result).isError());

    QCOMPARE(result.removed, 0);
    const QList<core::MediaRecord> records = loadAll(store);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.constFirst().id, original.id);
    QCOMPARE(records.constFirst().fingerprint, original.fingerprint);
    QCOMPARE(records.constFirst().managedOriginalPath, original.managedOriginalPath);
    QCOMPARE(result.warnings.size(), 1);
}

void TestScanIncremental::scanRemovesDeletedFile()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        const QString path = kRoot + "/photo.jpg";
        addFile(fs, path);

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};

        scanner.scan({kRoot}, cancel);
        QCOMPARE(loadAll(store).size(), 1);

        core::Error removeErr;
        fs.remove(path, &removeErr);
        QVERIFY(!removeErr.isError());

        Scanner::Result result;
        scanner.scan({kRoot}, cancel, &result);

        QCOMPARE(result.removed, 0);
        QCOMPARE(loadAll(store).size(), 1);
    }

    void TestScanIncremental::scanMigratesAnAvailableReferencedRecord()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);
        fs.addDirectory(kRoot);
        const QString path = addFile(fs, kRoot + "/legacy.jpg", "legacy");

        core::MediaRecord legacy;
        legacy.id = core::MediaId(QStringLiteral("legacy"));
        legacy.identity = fs.identify(path, nullptr);
        legacy.fingerprint = MediaHasher::computeFingerprint("legacy");
        QVERIFY(store.stage(legacy, nullptr));
        QVERIFY(store.commit(QStringLiteral("Referenced record"), nullptr).has_value());

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        QVERIFY(!scanner.scan({kRoot}, cancel, &result).isError());
        QCOMPARE(result.updated, 1);
        const auto migrated = store.load(legacy.id, nullptr);
        QVERIFY(migrated.has_value());
        QCOMPARE(migrated->originalStorage, core::MediaRecord::OriginalStorage::Managed);
        QVERIFY(!migrated->managedOriginalPath.isEmpty());
    }

    void TestScanIncremental::scanRetainsAMissingReferencedRecordForMigration()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);
        fs.addDirectory(kRoot);

        core::MediaRecord legacy;
        legacy.id = core::MediaId(QStringLiteral("missing-legacy"));
        legacy.identity.absolutePath = kRoot + QStringLiteral("/missing.jpg");
        legacy.identity.sizeBytes = 10;
        QVERIFY(store.stage(legacy, nullptr));
        QVERIFY(store.commit(QStringLiteral("Referenced record"), nullptr).has_value());

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        QVERIFY(!scanner.scan({kRoot}, cancel, &result).isError());
        QCOMPARE(result.removed, 0);
        const auto retained = store.load(legacy.id, nullptr);
        QVERIFY(retained.has_value());
        QCOMPARE(retained->originalStorage, core::MediaRecord::OriginalStorage::Referenced);
    }

void TestScanIncremental::scanDetectsRenameInSameDirectory()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        const QString oldPath = kRoot + "/old-name.jpg";
        addFile(fs, oldPath, "photo-content");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};

        scanner.scan({kRoot}, cancel);
        const core::MediaId originalId = loadAll(store)[0].id;

        // Rename: add under new name, remove old.
        const QString newPath = kRoot + "/new-name.jpg";
        addFile(fs, newPath, "photo-content");
        core::Error removeErr;
        fs.remove(oldPath, &removeErr);

        Scanner::Result result;
        scanner.scan({kRoot}, cancel, &result);

        QCOMPARE(result.updated, 1); // moved = updated
        QCOMPARE(result.removed, 0);
        QCOMPARE(result.added, 0);

        QCOMPARE(loadAll(store).size(), 1);
        QCOMPARE(loadAll(store)[0].id, originalId); // same id after rename
        QCOMPARE(loadAll(store)[0].identity.absolutePath, newPath);
    }

void TestScanIncremental::scanDetectsMoveToNewDirectory()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        fs.addDirectory(kRoot + "/sub");
        const QString oldPath = kRoot + "/photo.jpg";
        addFile(fs, oldPath, "photo-content");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};

        scanner.scan({kRoot}, cancel);
        const core::MediaId originalId = loadAll(store)[0].id;

        // Move to sub-directory.
        const QString newPath = kRoot + "/sub/photo.jpg";
        addFile(fs, newPath, "photo-content");
        core::Error removeErr;
        fs.remove(oldPath, &removeErr);

        Scanner::Result result;
        scanner.scan({kRoot}, cancel, &result);

        QCOMPARE(loadAll(store).size(), 1);
        QCOMPARE(loadAll(store)[0].id, originalId);
        QCOMPARE(loadAll(store)[0].identity.absolutePath, newPath);
        QCOMPARE(result.updated, 1);
        QCOMPARE(result.removed, 0);
    }

void TestScanIncremental::scanRecordsDuplicatesWithSeparateIds()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        fs.addDirectory(kRoot + "/sub");
        addFile(fs, kRoot + "/photo.jpg", "same-content");
        addFile(fs, kRoot + "/sub/copy.jpg", "same-content");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        scanner.scan({kRoot}, cancel, &result);

        QCOMPARE(result.added, 2);
        const QList<core::MediaRecord> records = loadAll(store);
        QCOMPARE(records.size(), 2);

        // Different ids.
        QVERIFY(records[0].id != records[1].id);
        // Same fingerprint.
        QCOMPARE(records[0].fingerprint, records[1].fingerprint);
    }
