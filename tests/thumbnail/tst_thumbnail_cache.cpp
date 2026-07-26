#include "pimio/thumbnail/thumbnail_disk_cache.h"

#include "pimio/core/types.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::thumbnail;
using namespace pimio::core;

class TestThumbnailCache : public QObject
{
    Q_OBJECT

private slots:
    void storeAndLoadRoundTrip();
    void missingEntryReturnsNullopt();
    void containsReturnsFalseForMissingKey();
    void containsReturnsTrueAfterStore();
    void emptyBytesAreRejectedAsCorrupt();
    void zeroByteCachedFileIsTreatedAsCorrupt();
    void invalidationRemovesAllVariantsForFingerprint();
    void invalidationOfInvalidFingerprintIsNoOp();
    void trimEvictsOldestEntriesFirst();
    void trimIsNoOpWhenUnderLimit();
    void trimIsNoOpWithNoLimit();
    void totalSizeBytesReflectsStoredContent();
};

static const QByteArray kFakeJpeg = QByteArray("FAKE_JPEG_DATA_12345", 20);
static const QByteArray kFakeJpeg2 = QByteArray("OTHER_JPEG_DATA_67890", 21);

void TestThumbnailCache::storeAndLoadRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    const QString key = QStringLiteral("sha256-abc123/thumbnail/160x160/0/r0");

    QVERIFY(cache.store(key, kFakeJpeg));
    auto loaded = cache.load(key);
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, kFakeJpeg);
}

void TestThumbnailCache::missingEntryReturnsNullopt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    auto loaded = cache.load(QStringLiteral("sha256-noentry/thumbnail/160x160/0/r0"));
    QVERIFY(!loaded.has_value());
}

void TestThumbnailCache::containsReturnsFalseForMissingKey()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    QVERIFY(!cache.contains(QStringLiteral("sha256-ghost/thumbnail/160x160/0/r0")));
}

void TestThumbnailCache::containsReturnsTrueAfterStore()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    const QString key = QStringLiteral("sha256-abc123/thumbnail/160x160/0/r0");
    QVERIFY(cache.store(key, kFakeJpeg));
    QVERIFY(cache.contains(key));
}

void TestThumbnailCache::emptyBytesAreRejectedAsCorrupt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    const QString key = QStringLiteral("sha256-abc/thumbnail/160x160/0/r0");
    // store() rejects empty bytes.
    QVERIFY(!cache.store(key, QByteArray()));
    QVERIFY(!cache.contains(key));
}

void TestThumbnailCache::zeroByteCachedFileIsTreatedAsCorrupt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    const QString key = QStringLiteral("sha256-bad/thumbnail/160x160/0/r0");

    // Manually write a zero-byte file where the cache entry should be.
    const QString path = tmp.path() + QStringLiteral("/sha256-bad/thumbnail/160x160/0/r0");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    QVERIFY(!cache.contains(key));
    QVERIFY(!cache.load(key).has_value());

    // A subsequent store() should replace it successfully.
    QVERIFY(cache.store(key, kFakeJpeg));
    QVERIFY(cache.contains(key));
}

void TestThumbnailCache::invalidationRemovesAllVariantsForFingerprint()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    const ContentFingerprint fp(QStringLiteral("sha256"), QStringLiteral("abc123def456"));

    // Store two variants for the same fingerprint.
    const QString key160 = fp.cacheKey() + QStringLiteral("/thumbnail/160x160/0/r0");
    const QString key320 = fp.cacheKey() + QStringLiteral("/thumbnail/320x320/0/r0");
    QVERIFY(cache.store(key160, kFakeJpeg));
    QVERIFY(cache.store(key320, kFakeJpeg2));

    QVERIFY(cache.contains(key160));
    QVERIFY(cache.contains(key320));

    cache.invalidate(fp);

    QVERIFY(!cache.contains(key160));
    QVERIFY(!cache.contains(key320));
}

void TestThumbnailCache::invalidationOfInvalidFingerprintIsNoOp()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    // Should not crash or return an error.
    cache.invalidate(ContentFingerprint());
}

void TestThumbnailCache::trimEvictsOldestEntriesFirst()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Each entry is exactly 20 bytes. Limit to 40 bytes so one entry fits.
    ThumbnailDiskCache cache(tmp.path(), 40);

    // Store two 20-byte entries. After the second, total = 40 — within limit.
    QVERIFY(cache.store(QStringLiteral("sha256-aaa/thumbnail/160x160/0/r0"), kFakeJpeg));
    QTest::qWait(50); // ensure distinct modification times
    QVERIFY(cache.store(QStringLiteral("sha256-bbb/thumbnail/160x160/0/r0"), kFakeJpeg));

    QCOMPARE(cache.totalSizeBytes(), 40LL);

    // Store a third entry (total becomes 60 > 40), then trim.
    QVERIFY(cache.store(QStringLiteral("sha256-ccc/thumbnail/160x160/0/r0"), kFakeJpeg));
    cache.trim();

    // Total should now be ≤ the 40-byte limit.
    QVERIFY(cache.totalSizeBytes() <= 40);

    // The oldest entry ('aaa') should have been evicted.
    QVERIFY(!cache.contains(QStringLiteral("sha256-aaa/thumbnail/160x160/0/r0")));
}

void TestThumbnailCache::trimIsNoOpWhenUnderLimit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path(), 1024 * 1024);
    QVERIFY(cache.store(QStringLiteral("sha256-aaa/thumbnail/160x160/0/r0"), kFakeJpeg));

    const qint64 before = cache.totalSizeBytes();
    cache.trim();
    QCOMPARE(cache.totalSizeBytes(), before);
}

void TestThumbnailCache::trimIsNoOpWithNoLimit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path(), -1);
    QVERIFY(cache.store(QStringLiteral("sha256-aaa/thumbnail/160x160/0/r0"), kFakeJpeg));

    const qint64 before = cache.totalSizeBytes();
    cache.trim();
    QCOMPARE(cache.totalSizeBytes(), before);
}

void TestThumbnailCache::totalSizeBytesReflectsStoredContent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());

    QCOMPARE(cache.totalSizeBytes(), 0LL);

    QVERIFY(cache.store(QStringLiteral("sha256-aaa/thumbnail/160x160/0/r0"), kFakeJpeg));
    QCOMPARE(cache.totalSizeBytes(), static_cast<qint64>(kFakeJpeg.size()));

    QVERIFY(cache.store(QStringLiteral("sha256-bbb/thumbnail/160x160/0/r0"), kFakeJpeg2));
    QCOMPARE(cache.totalSizeBytes(),
             static_cast<qint64>(kFakeJpeg.size() + kFakeJpeg2.size()));

    const ContentFingerprint fp(QStringLiteral("sha256"), QStringLiteral("aaa"));
    cache.invalidate(fp);
    QCOMPARE(cache.totalSizeBytes(), static_cast<qint64>(kFakeJpeg2.size()));
}

QTEST_MAIN(TestThumbnailCache)

#include "tst_thumbnail_cache.moc"
