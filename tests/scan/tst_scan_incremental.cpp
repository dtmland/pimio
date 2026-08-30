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

void TestScanIncremental::scanSkipsSymlinksByDefault()
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

void TestScanIncremental::scanFollowsSymlinksWhenEnabled()
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

void TestScanIncremental::scanRecordsPermissionFailureAsWarning()
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

void TestScanIncremental::scanHandlesDisappearingFileBetweenListAndRead()
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

void TestScanIncremental::scanIsIdempotentAfterRestart()
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

void TestScanIncremental::scanReturnsErrorForMissingRoot()
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

void TestScanIncremental::scanCancellationDiscardsChanges()
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

void TestScanIncremental::scanUsesMetadataReaderWhenAvailable()
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

void TestScanIncremental::largeTreeBenchmark()
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

QTEST_APPLESS_MAIN(TestScanIncremental)
