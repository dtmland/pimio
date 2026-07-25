#include "pimio/core/durable_store.h"
#include "pimio/core/media_request.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/fake_metadata_reader.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/memory_file_system.h"
#include "pimio/testing/qtest_printers.h"
#include "pimio/testing/recording_media_request_service.h"

#include <QTest>

using namespace pimio::core;
using namespace pimio::testing;

namespace {

MediaRecord makeRecord(const QString &id, const QString &caption)
{
    MediaRecord record;
    record.id = MediaId(id);
    record.fingerprint = ContentFingerprint(QStringLiteral("sha256"), id);
    record.metadata.kind = MediaKind::Image;
    record.metadata.caption = caption;
    return record;
}

MediaRequest makeRequest(const QString &digest, MediaRequestKind kind = MediaRequestKind::Thumbnail)
{
    MediaRequest request;
    request.mediaId = MediaId(digest);
    request.fingerprint = ContentFingerprint(QStringLiteral("sha256"), digest);
    request.kind = kind;
    request.targetSize = QSize(256, 256);
    return request;
}

} // namespace

/// Contract tests for the abstract boundaries.
///
/// They run entirely against in-memory fakes, with no display, no network, and
/// no filesystem access, so they behave identically on every platform runner.
class TestCoreContracts : public QObject
{
    Q_OBJECT

private slots:
    void fakeClockIsDeterministic();
    void fileSystemReportsInjectedFailures();
    void atomicWriteKeepsPreviousFileWhenItFails();
    void directoryListingIsDeterministic();
    void metadataReaderReportsUnsupportedMediaWithoutFailing();
    void durableStoreSeparatesStagingFromHistory();
    void failedCommitKeepsStagedChangesRecoverable();
    void externalChangeChangesStateToken();
    void unavailableStoreFailsVisibly();
    void mediaRequestCacheKeysDistinguishOutputs();
    void mediaRequestCancellationIsIdempotent();
    void mediaRequestServiceCancelsScrolledAwayWork();
};

void TestCoreContracts::fakeClockIsDeterministic()
{
    FakeClock clock(QDateTime(QDate(2024, 3, 10), QTime(1, 30), Qt::UTC));
    QCOMPARE(clock.monotonicMSecs(), 0);

    clock.advance(1500);
    QCOMPARE(clock.monotonicMSecs(), 1500);
    QCOMPARE(clock.nowUtc(), QDateTime(QDate(2024, 3, 10), QTime(1, 30, 1, 500), Qt::UTC));

    // A backwards wall-clock correction must never move monotonic time.
    clock.setWallClock(QDateTime(QDate(2024, 3, 10), QTime(0, 0), Qt::UTC));
    QCOMPARE(clock.monotonicMSecs(), 1500);
    clock.advance(500);
    QCOMPARE(clock.monotonicMSecs(), 2000);
}

void TestCoreContracts::fileSystemReportsInjectedFailures()
{
    MemoryFileSystem fs;
    fs.addFile(QStringLiteral("/library/a.jpg"), QByteArrayLiteral("jpeg-bytes"));
    fs.injectFailure(QStringLiteral("/library/locked.jpg"), ErrorCode::PermissionDenied);
    fs.addFile(QStringLiteral("/library/locked.jpg"), QByteArrayLiteral("x"));

    Error error;
    QCOMPARE(fs.readAll(QStringLiteral("/library/a.jpg"), &error),
             QByteArrayLiteral("jpeg-bytes"));
    QVERIFY(!error.isError());

    fs.readAll(QStringLiteral("/library/locked.jpg"), &error);
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::PermissionDenied);
    QCOMPARE(error.context().value(QStringLiteral("path")).toString(),
             QStringLiteral("/library/locked.jpg"));

    Error missingError;
    fs.readAll(QStringLiteral("/library/gone.jpg"), &missingError);
    PIMIO_COMPARE_ENUM(missingError.code(), ErrorCode::NotFound);
}

void TestCoreContracts::atomicWriteKeepsPreviousFileWhenItFails()
{
    MemoryFileSystem fs;
    fs.addFile(QStringLiteral("/library/a.xmp"), QByteArrayLiteral("original"));
    fs.setAvailableSpaceBytes(4);

    Error error;
    QVERIFY(!fs.writeAtomically(QStringLiteral("/library/a.xmp"),
                                QByteArrayLiteral("a much longer replacement"), &error));
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::OutOfSpace);
    QCOMPARE(fs.writeCount(), 0);
    QCOMPARE(fs.readAll(QStringLiteral("/library/a.xmp"), nullptr),
             QByteArrayLiteral("original"));

    fs.setAvailableSpaceBytes(-1);
    Error writeError;
    QVERIFY(fs.writeAtomically(QStringLiteral("/library/a.xmp"), QByteArrayLiteral("updated"),
                               &writeError));
    QVERIFY(!writeError.isError());
    QCOMPARE(fs.writeCount(), 1);
    QCOMPARE(fs.readAll(QStringLiteral("/library/a.xmp"), nullptr), QByteArrayLiteral("updated"));
}

void TestCoreContracts::directoryListingIsDeterministic()
{
    MemoryFileSystem fs;
    fs.addFile(QStringLiteral("/library/c.jpg"), QByteArrayLiteral("c"));
    fs.addFile(QStringLiteral("/library/a.jpg"), QByteArrayLiteral("a"));
    fs.addFile(QStringLiteral("/library/b.jpg"), QByteArrayLiteral("b"));
    fs.addDirectory(QStringLiteral("/library/sub"));
    fs.addFile(QStringLiteral("/library/sub/d.jpg"), QByteArrayLiteral("d"));
    fs.addSymbolicLink(QStringLiteral("/library/link.jpg"), QStringLiteral("/library/a.jpg"));

    Error error;
    const QList<DirectoryEntry> entries = fs.listDirectory(QStringLiteral("/library"), &error);
    QVERIFY(!error.isError());

    QStringList names;
    for (const DirectoryEntry &entry : entries) {
        names.append(entry.fileName);
    }
    // Sub-directory contents are not included; order is by path.
    QCOMPARE(names,
             QStringList({QStringLiteral("a.jpg"), QStringLiteral("b.jpg"),
                          QStringLiteral("c.jpg"), QStringLiteral("link.jpg"),
                          QStringLiteral("sub")}));

    QVERIFY(entries.at(3).isSymbolicLink);
    QVERIFY(entries.at(4).isDirectory);

    // File identity distinguishes a rename from a different file.
    const FileIdentity a = fs.identify(QStringLiteral("/library/a.jpg"), nullptr);
    FileIdentity renamed = a;
    renamed.absolutePath = QStringLiteral("/library/renamed.jpg");
    QVERIFY(a.sameFileAs(renamed));
    QVERIFY(!a.sameFileAs(fs.identify(QStringLiteral("/library/b.jpg"), nullptr)));
    QVERIFY(a.looksUnchangedFrom(renamed));
}

void TestCoreContracts::metadataReaderReportsUnsupportedMediaWithoutFailing()
{
    FakeMetadataReader reader;

    MetadataReadResult result;
    result.metadata.kind = MediaKind::Image;
    result.metadata.fileName = QStringLiteral("a.jpg");
    result.usedSidecar = true;
    reader.addResult(QStringLiteral("/library/a.jpg"), result);
    reader.addUnreadable(QStringLiteral("/library/corrupt.jpg"), ErrorCode::CorruptData);

    QVERIFY(reader.supports(QStringLiteral("/library/a.jpg")));
    QVERIFY(!reader.supports(QStringLiteral("/library/notes.txt")));

    Error error;
    const auto good = reader.read(QStringLiteral("/library/a.jpg"), &error);
    QVERIFY(good.has_value());
    QVERIFY(good->usedSidecar);
    QVERIFY(!error.isError());

    Error corruptError;
    QVERIFY(!reader.read(QStringLiteral("/library/corrupt.jpg"), &corruptError).has_value());
    PIMIO_COMPARE_ENUM(corruptError.code(), ErrorCode::CorruptData);

    Error unsupportedError;
    QVERIFY(!reader.read(QStringLiteral("/library/notes.txt"), &unsupportedError).has_value());
    PIMIO_COMPARE_ENUM(unsupportedError.code(), ErrorCode::UnsupportedMedia);

    QCOMPARE(reader.readPaths().size(), 3);
}

void TestCoreContracts::durableStoreSeparatesStagingFromHistory()
{
    FakeClock clock(QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC));
    MemoryDurableStore store(clock);

    const QString tokenBefore = store.stateToken();

    Error error;
    QVERIFY(store.stage(makeRecord(QStringLiteral("m-1"), QStringLiteral("first")), &error));
    QVERIFY(store.hasStagedChanges());

    // Staged work is not visible as committed state.
    Error loadError;
    QVERIFY(!store.load(MediaId(QStringLiteral("m-1")), &loadError).has_value());
    PIMIO_COMPARE_ENUM(loadError.code(), ErrorCode::NotFound);
    QCOMPARE(store.stateToken(), tokenBefore);
    QVERIFY(store.history(-1, nullptr).isEmpty());

    clock.advance(60'000);
    const auto checkpoint = store.commit(QStringLiteral("Save captions"), &error);
    QVERIFY(checkpoint.has_value());
    QCOMPARE(checkpoint->message, QStringLiteral("Save captions"));
    QCOMPARE(checkpoint->createdAtUtc, QDateTime(QDate(2024, 1, 1), QTime(12, 1), Qt::UTC));
    QVERIFY(!store.hasStagedChanges());
    QVERIFY(store.stateToken() != tokenBefore);

    const auto loaded = store.load(MediaId(QStringLiteral("m-1")), nullptr);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->metadata.caption, QStringLiteral("first"));
    QCOMPARE(store.listIds(nullptr).size(), 1);
    PIMIO_COMPARE_ID(store.listIds(nullptr).constFirst(), MediaId(QStringLiteral("m-1")));
    QCOMPARE(store.history(-1, nullptr).size(), 1);

    QVERIFY(store.stage(makeRecord(QStringLiteral("m-2"), QStringLiteral("second")), nullptr));
    QVERIFY(store.discardStaged(nullptr));
    QVERIFY(!store.hasStagedChanges());
    QVERIFY(!store.load(MediaId(QStringLiteral("m-2")), nullptr).has_value());
}

void TestCoreContracts::failedCommitKeepsStagedChangesRecoverable()
{
    FakeClock clock(QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC));
    MemoryDurableStore store(clock);

    QVERIFY(store.stage(makeRecord(QStringLiteral("m-1"), QStringLiteral("draft")), nullptr));
    const QString tokenBefore = store.stateToken();

    store.failNextCommit(ErrorCode::OutOfSpace);
    Error error;
    QVERIFY(!store.commit(QStringLiteral("Save"), &error).has_value());
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::OutOfSpace);

    // An uncommitted change is never reported as committed, and it is not lost.
    QVERIFY(store.hasStagedChanges());
    QCOMPARE(store.stateToken(), tokenBefore);
    QVERIFY(!store.load(MediaId(QStringLiteral("m-1")), nullptr).has_value());
    QVERIFY(store.history(-1, nullptr).isEmpty());

    QVERIFY(store.commit(QStringLiteral("Save"), nullptr).has_value());
    QVERIFY(store.load(MediaId(QStringLiteral("m-1")), nullptr).has_value());
}

void TestCoreContracts::externalChangeChangesStateToken()
{
    FakeClock clock(QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC));
    MemoryDurableStore store(clock);

    QVERIFY(store.stage(makeRecord(QStringLiteral("m-1"), QStringLiteral("mine")), nullptr));
    QVERIFY(store.commit(QStringLiteral("Save"), nullptr).has_value());
    const QString tokenAfterCommit = store.stateToken();

    store.applyExternalChange(makeRecord(QStringLiteral("m-9"), QStringLiteral("from cli")));

    QVERIFY(store.stateToken() != tokenAfterCommit);
    QCOMPARE(store.listIds(nullptr).size(), 2);
    QCOMPARE(store.history(1, nullptr).size(), 1);
}

void TestCoreContracts::unavailableStoreFailsVisibly()
{
    FakeClock clock(QDateTime(QDate(2024, 1, 1), QTime(12, 0), Qt::UTC));
    MemoryDurableStore store(clock);
    store.setAvailable(false);

    QVERIFY(!store.isAvailable());

    Error stageError;
    QVERIFY(!store.stage(makeRecord(QStringLiteral("m-1"), QString()), &stageError));
    PIMIO_COMPARE_ENUM(stageError.code(), ErrorCode::StorageUnavailable);
    QVERIFY(stageError.isRetryable());

    Error commitError;
    QVERIFY(!store.commit(QStringLiteral("Save"), &commitError).has_value());
    PIMIO_COMPARE_ENUM(commitError.code(), ErrorCode::StorageUnavailable);

    Error loadError;
    QVERIFY(!store.load(MediaId(QStringLiteral("m-1")), &loadError).has_value());
    PIMIO_COMPARE_ENUM(loadError.code(), ErrorCode::StorageUnavailable);
}

void TestCoreContracts::mediaRequestCacheKeysDistinguishOutputs()
{
    const MediaRequest base = makeRequest(QStringLiteral("abc"));

    MediaRequest sameContentDifferentItem = base;
    sameContentDifferentItem.mediaId = MediaId(QStringLiteral("other"));
    QCOMPARE(sameContentDifferentItem.cacheKey(), base.cacheKey());

    MediaRequest differentSize = base;
    differentSize.targetSize = QSize(512, 512);
    QVERIFY(differentSize.cacheKey() != base.cacheKey());

    MediaRequest differentKind = base;
    differentKind.kind = MediaRequestKind::Preview;
    QVERIFY(differentKind.cacheKey() != base.cacheKey());

    MediaRequest differentPosition = base;
    differentPosition.positionMs = 1000;
    QVERIFY(differentPosition.cacheKey() != base.cacheKey());

    MediaRequest differentRecipe = base;
    differentRecipe.recipeRevision = 2;
    QVERIFY(differentRecipe.cacheKey() != base.cacheKey());

    MediaRequest differentContent = makeRequest(QStringLiteral("def"));
    QVERIFY(differentContent.cacheKey() != base.cacheKey());

    // Priority is a scheduling hint, not part of the produced bytes.
    MediaRequest differentPriority = base;
    differentPriority.priority = JobPriority::Background;
    QCOMPARE(differentPriority.cacheKey(), base.cacheKey());
}

void TestCoreContracts::mediaRequestCancellationIsIdempotent()
{
    RecordingMediaRequestService service;

    int errors = 0;
    const MediaRequestHandle handle = service.request(
            makeRequest(QStringLiteral("abc")), nullptr,
            [&errors](const MediaRequest &, const Error &error) {
                PIMIO_COMPARE_ENUM(error.code(), ErrorCode::Cancelled);
                ++errors;
            });

    QVERIFY(handle.isValid());
    service.cancel(handle);
    service.cancel(handle);
    service.cancel(MediaRequestHandle(9999));

    QCOMPARE(errors, 1);
    QCOMPARE(service.cancelledCount(), 1);
    QVERIFY(service.pendingCacheKeys().isEmpty());
    QVERIFY(!service.complete(handle, MediaResult()));
}

void TestCoreContracts::mediaRequestServiceCancelsScrolledAwayWork()
{
    RecordingMediaRequestService service;

    QList<MediaRequestHandle> handles;
    for (int i = 0; i < 5; ++i) {
        handles.append(service.request(makeRequest(QStringLiteral("item-%1").arg(i)), nullptr,
                                       nullptr));
    }
    QCOMPARE(service.requestedCacheKeys().size(), 5);
    QCOMPARE(service.pendingCacheKeys().size(), 5);

    // The view scrolled: only the last two remain visible.
    service.cancelAllExcept({handles.at(3), handles.at(4)});

    QCOMPARE(service.cancelledCount(), 3);
    QCOMPARE(service.pendingCacheKeys(),
             QStringList({makeRequest(QStringLiteral("item-3")).cacheKey(),
                          makeRequest(QStringLiteral("item-4")).cacheKey()}));

    MediaResult result;
    result.format = QStringLiteral("jpeg");
    result.actualSize = QSize(256, 171);

    bool delivered = false;
    const MediaRequestHandle progressive = service.request(
            makeRequest(QStringLiteral("item-9"), MediaRequestKind::Preview),
            [&delivered](const MediaRequest &, const MediaResult &value) {
                delivered = true;
                QCOMPARE(value.format, QStringLiteral("jpeg"));
            },
            nullptr);
    QVERIFY(service.complete(progressive, result));
    QVERIFY(delivered);
}

QTEST_APPLESS_MAIN(TestCoreContracts)

#include "tst_core_contracts.moc"
