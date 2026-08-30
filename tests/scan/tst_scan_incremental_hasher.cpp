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

void TestScanIncremental::hasherProducesConsistentFingerprint()
    {
        const QByteArray data = "hello world";
        const core::ContentFingerprint fp1 = MediaHasher::computeFingerprint(data);
        const core::ContentFingerprint fp2 = MediaHasher::computeFingerprint(data);

        QVERIFY(fp1.isValid());
        QCOMPARE(fp1.algorithm(), QStringLiteral("sha256"));
        QCOMPARE(fp1, fp2);
    }

void TestScanIncremental::hasherDifferentDataProducesDifferentFingerprint()
    {
        const core::ContentFingerprint fp1 = MediaHasher::computeFingerprint("aaa");
        const core::ContentFingerprint fp2 = MediaHasher::computeFingerprint("bbb");
        QVERIFY(fp1 != fp2);
    }

void TestScanIncremental::hasherReadFromFileSystem()
    {
        testing::MemoryFileSystem fs;
        fs.addFile(QStringLiteral("/img.jpg"), "content");

        core::Error err;
        const core::ContentFingerprint fp =
            MediaHasher::fingerprintFile(QStringLiteral("/img.jpg"), fs, &err);
        QVERIFY(!err.isError());
        QCOMPARE(fp, MediaHasher::computeFingerprint("content"));
    }

