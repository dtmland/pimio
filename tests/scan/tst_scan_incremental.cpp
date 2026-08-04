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

namespace {

const QString kRoot = QStringLiteral("/library");
const QDateTime kT0 = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

testing::FakeClock makeClock()
{
    return testing::FakeClock(kT0);
}

/// Adds a plain file to the MemoryFileSystem and returns its path.
QString addFile(testing::MemoryFileSystem &fs, const QString &path,
                const QByteArray &contents = "test-content",
                const QDateTime &modified = kT0)
{
    fs.addFile(path, contents, modified);
    return path;
}

/// Loads all MediaRecords from the store.
QList<core::MediaRecord> loadAll(testing::MemoryDurableStore &store)
{
    core::Error err;
    const QList<core::MediaId> ids = store.listIds(&err);
    Q_ASSERT(!err.isError());
    QList<core::MediaRecord> records;
    for (const core::MediaId &id : ids) {
        core::Error loadErr;
        auto rec = store.load(id, &loadErr);
        Q_ASSERT(rec.has_value());
        records.append(*rec);
    }
    return records;
}

} // namespace

class TestScanIncremental : public QObject
{
    Q_OBJECT

private slots:
    // ---- MediaHasher ----

    void hasherProducesConsistentFingerprint()
    {
        const QByteArray data = "hello world";
        const core::ContentFingerprint fp1 = MediaHasher::computeFingerprint(data);
        const core::ContentFingerprint fp2 = MediaHasher::computeFingerprint(data);

        QVERIFY(fp1.isValid());
        QCOMPARE(fp1.algorithm(), QStringLiteral("sha256"));
        QCOMPARE(fp1, fp2);
    }

    void hasherDifferentDataProducesDifferentFingerprint()
    {
        const core::ContentFingerprint fp1 = MediaHasher::computeFingerprint("aaa");
        const core::ContentFingerprint fp2 = MediaHasher::computeFingerprint("bbb");
        QVERIFY(fp1 != fp2);
    }

    void hasherReadFromFileSystem()
    {
        testing::MemoryFileSystem fs;
        fs.addFile(QStringLiteral("/img.jpg"), "content");

        core::Error err;
        const core::ContentFingerprint fp =
            MediaHasher::fingerprintFile(QStringLiteral("/img.jpg"), fs, &err);
        QVERIFY(!err.isError());
        QCOMPARE(fp, MediaHasher::computeFingerprint("content"));
    }

    // ---- Scanner: add ----

    void scanAddsNewFiles()
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
    }

    void scanSkipsNonMediaFilesLikeDsStore()
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

    void scanRemovesAPreviouslyIndexedFileThatIsNoLongerMedia()
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
        QCOMPARE(second.removed, 1);
        QVERIFY(loadAll(store).isEmpty());
    }

    // ---- Scanner: unchanged ----

    void repeatedUnchangedScanMakesNoUpdates()
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

    void scanRemovesHistoricalSamePathDuplicates()
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
        QCOMPARE(result.unchanged, 1);
        QCOMPARE(result.removed, 1);
        QCOMPARE(loadAll(store).size(), 1);
        QCOMPARE(loadAll(store).constFirst().identity.absolutePath, path);
    }

    // ---- Scanner: update ----

    void scanUpdatesChangedFile()
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
    }

    // ---- Scanner: delete ----

    void scanRemovesDeletedFile()
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

        QCOMPARE(result.removed, 1);
        QCOMPARE(loadAll(store).size(), 0);
    }

    // ---- Scanner: rename ----

    void scanDetectsRenameInSameDirectory()
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

    // ---- Scanner: move ----

    void scanDetectsMoveToNewDirectory()
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

    // ---- Scanner: duplicate ----

    void scanRecordsDuplicatesWithSeparateIds()
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

    // ---- Scanner: symlink policy ----

    void scanSkipsSymlinksByDefault()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        fs.addFile(kRoot + "/real.jpg", "content");
        fs.addSymbolicLink(kRoot + "/link.jpg", kRoot + "/real.jpg");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        scanner.scan({kRoot, /*followSymlinks=*/false}, cancel, &result);

        QCOMPARE(result.added, 1); // only real.jpg
        const QList<core::MediaRecord> records = loadAll(store);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].identity.absolutePath, kRoot + "/real.jpg");
    }

    void scanFollowsSymlinksWhenEnabled()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        fs.addFile(kRoot + "/real.jpg", "real");
        fs.addSymbolicLink(kRoot + "/link.jpg", kRoot + "/real.jpg");

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        scanner.scan({kRoot, /*followSymlinks=*/true}, cancel, &result);

        QCOMPARE(result.added, 2);
    }

    // ---- Scanner: permission failure ----

    void scanRecordsPermissionFailureAsWarning()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/readable.jpg", "ok");
        addFile(fs, kRoot + "/locked.jpg", "secret");
        fs.injectFailure(kRoot + "/locked.jpg", core::ErrorCode::PermissionDenied);

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan({kRoot}, cancel, &result);

        QVERIFY(!err.isError());           // hard failure? no
        QCOMPARE(result.added, 1);         // readable.jpg added
        QCOMPARE(result.warnings.size(), 1); // locked.jpg warned
    }

    // ---- Scanner: disappearing file ----

    void scanHandlesDisappearingFileBetweenListAndRead()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/stable.jpg", "ok");
        addFile(fs, kRoot + "/vanishing.jpg", "soon-gone");
        // The file appears in listDirectory but readAll fails.
        fs.injectFailure(kRoot + "/vanishing.jpg", core::ErrorCode::NotFound);

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan({kRoot}, cancel, &result);

        QVERIFY(!err.isError());
        QCOMPARE(result.added, 1); // only stable.jpg
        QCOMPARE(result.warnings.size(), 1);
    }

    // ---- Scanner: restart ----

    void scanIsIdempotentAfterRestart()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        addFile(fs, kRoot + "/a.jpg", "a");
        addFile(fs, kRoot + "/b.jpg", "b");
        addFile(fs, kRoot + "/c.jpg", "c");

        // First scan: committed to store.
        {
            Scanner scanner(&fs, &reader, &store);
            std::atomic<bool> cancel{false};
            scanner.scan({kRoot}, cancel);
        }
        QCOMPARE(loadAll(store).size(), 3);
        const QString token1 = store.stateToken();

        // Simulate restart: new Scanner with same store, same fs.
        {
            Scanner scanner(&fs, &reader, &store);
            std::atomic<bool> cancel{false};
            Scanner::Result result;
            scanner.scan({kRoot}, cancel, &result);

            QCOMPARE(result.added, 0);
            QCOMPARE(result.updated, 0);
            QCOMPARE(result.removed, 0);
            QCOMPARE(result.unchanged, 3);
        }

        // State token must not change when there were no real updates.
        QCOMPARE(store.stateToken(), token1);
    }

    // ---- Scanner: unavailable root ----

    void scanReturnsErrorForMissingRoot()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        // Do NOT add kRoot to fs.
        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        const core::Error err = scanner.scan({kRoot}, cancel);

        QVERIFY(err.isError());
        PIMIO_COMPARE_ENUM(err.code(), core::ErrorCode::NotFound);
    }

    // ---- Scanner: cancellation ----

    void scanCancellationDiscardsChanges()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        for (int i = 0; i < 10; ++i) {
            addFile(fs, kRoot + QStringLiteral("/%1.jpg").arg(i),
                    QByteArray("content") + QByteArray::number(i));
        }

        // Cancel before the scan begins.
        std::atomic<bool> cancel{true};
        Scanner scanner(&fs, &reader, &store);
        const core::Error err = scanner.scan({kRoot}, cancel);

        QVERIFY(err.isError());
        PIMIO_COMPARE_ENUM(err.code(), core::ErrorCode::Cancelled);
        // No records should have been committed.
        QCOMPARE(loadAll(store).size(), 0);
    }

    // ---- Scanner: metadata reader integration ----

    void scanUsesMetadataReaderWhenAvailable()
    {
        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        const QString path = kRoot + "/photo.jpg";
        fs.addDirectory(kRoot);
        fs.addFile(path, "img-data");

        core::MediaMetadata metadata;
        metadata.kind = core::MediaKind::Image;
        metadata.cameraMake = QStringLiteral("Canon");
        metadata.cameraModel = QStringLiteral("EOS R5");
        reader.addResult(path, core::MetadataReadResult{metadata, false, {}});

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        scanner.scan({kRoot}, cancel);

        const QList<core::MediaRecord> records = loadAll(store);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].metadata.cameraMake, QStringLiteral("Canon"));
    }

    // ---- Scanner: large-tree benchmark ----

    void largeTreeBenchmark()
    {
        constexpr int kFiles = 1000;
        constexpr int kMaxMs = 5000; // generous budget for a debug build

        testing::MemoryFileSystem fs;
        testing::FakeMetadataReader reader;
        auto clock = makeClock();
        testing::MemoryDurableStore store(clock);

        fs.addDirectory(kRoot);
        for (int i = 0; i < kFiles; ++i) {
            const QString path =
                kRoot + QStringLiteral("/%1.jpg").arg(i, 5, 10, QLatin1Char('0'));
            fs.addFile(path, QByteArray("payload-") + QByteArray::number(i));
        }

        QElapsedTimer timer;
        timer.start();

        Scanner scanner(&fs, &reader, &store);
        std::atomic<bool> cancel{false};
        Scanner::Result result;
        const core::Error err = scanner.scan({kRoot}, cancel, &result);

        const qint64 elapsed = timer.elapsed();
        qDebug("largeTreeBenchmark: %d files in %lld ms", kFiles, elapsed);

        QVERIFY(!err.isError());
        QCOMPARE(result.added, kFiles);
        QCOMPARE(loadAll(store).size(), kFiles);
        QVERIFY2(elapsed < kMaxMs,
                 qPrintable(QStringLiteral("scan of %1 files took %2 ms (budget %3 ms)")
                                .arg(kFiles)
                                .arg(elapsed)
                                .arg(kMaxMs)));
    }
};

QTEST_APPLESS_MAIN(TestScanIncremental)
#include "tst_scan_incremental.moc"
