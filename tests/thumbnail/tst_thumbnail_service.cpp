#include "pimio/thumbnail/thumbnail_service.h"

#include "pimio/thumbnail/thumbnail_disk_cache.h"
#include "pimio/thumbnail/thumbnail_renderer.h"

#include "pimio/core/error.h"
#include "pimio/core/media_request.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::thumbnail;
using namespace pimio::core;

// ---------------------------------------------------------------------------
// Fake renderer shared by all sub-tests
// ---------------------------------------------------------------------------

class FakeRenderer final : public ThumbnailRenderer
{
public:
    // When non-null, rendering blocks until the semaphore is released.
    QSemaphore *blockOn = nullptr;

    // Records the cache key of every render call, in order.
    mutable QMutex mutex;
    mutable QStringList renderedKeys;

    // When set the renderer returns an error instead of bytes.
    mutable bool failNextRender = false;

    MediaResult render(const MediaRequest &request, Error *error) const override
    {
        if (blockOn) {
            blockOn->acquire();
        }

        QMutexLocker lock(&mutex);
        renderedKeys.append(request.cacheKey());

        if (failNextRender) {
            failNextRender = false;
            if (error) {
                *error = Error(ErrorCode::CorruptData,
                               QStringLiteral("injected render failure"));
            }
            return {};
        }

        MediaResult result;
        result.bytes = QByteArray("FAKE_THUMB");
        result.format = QStringLiteral("jpeg");
        result.actualSize = request.targetSize;
        return result;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MediaRequest makeRequest(const QString &digest,
                                JobPriority priority = JobPriority::Interactive)
{
    MediaRequest req;
    req.mediaId = MediaId(digest);
    req.fingerprint = ContentFingerprint(QStringLiteral("sha256"), digest);
    req.absolutePath = QStringLiteral("/fake/%1.jpg").arg(digest);
    req.kind = MediaRequestKind::Thumbnail;
    req.targetSize = QSize(160, 160);
    req.priority = priority;
    return req;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestThumbnailService : public QObject
{
    Q_OBJECT

private slots:
    void cacheMissRendersAndCaches();
    void cacheHitSkipsRender();
    void cancellationPreventsResultCallback();
    void cancelAllExceptLeavesNamedHandles();
    void renderErrorDeliversErrorCallback();
    void interactivePriorityBeforeBackground();
};

void TestThumbnailService::cacheMissRendersAndCaches()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    FakeRenderer renderer;

    ThumbnailService service(&cache, &renderer);

    bool gotResult = false;
    const MediaRequest req = makeRequest(QStringLiteral("abc"));
    service.request(
            req,
            [&](const MediaRequest &, const MediaResult &result) {
                gotResult = true;
                QCOMPARE(result.bytes, QByteArray("FAKE_THUMB"));
            },
            [&](const MediaRequest &, const Error &) { QFAIL("unexpected error"); });

    QTRY_VERIFY_WITH_TIMEOUT(gotResult, 5000);

    QCOMPARE(renderer.renderedKeys.size(), 1);
    QCOMPARE(renderer.renderedKeys.first(), req.cacheKey());
    QVERIFY(cache.contains(req.cacheKey()));
}

void TestThumbnailService::cacheHitSkipsRender()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    FakeRenderer renderer;

    const MediaRequest req = makeRequest(QStringLiteral("cached"));
    QVERIFY(cache.store(req.cacheKey(), QByteArray("CACHED_BYTES")));

    ThumbnailService service(&cache, &renderer);

    bool gotResult = false;
    service.request(
            req,
            [&](const MediaRequest &, const MediaResult &result) {
                gotResult = true;
                QCOMPARE(result.bytes, QByteArray("CACHED_BYTES"));
            },
            [&](const MediaRequest &, const Error &) { QFAIL("unexpected error"); });

    QTRY_VERIFY_WITH_TIMEOUT(gotResult, 5000);
    QCOMPARE(renderer.renderedKeys.size(), 0);
}

void TestThumbnailService::cancellationPreventsResultCallback()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    QSemaphore block(0);
    FakeRenderer renderer;
    renderer.blockOn = &block;

    ThumbnailService service(&cache, &renderer);
    service.setMaxConcurrency(1);

    bool gotResult = false;
    bool gotError = false;
    const MediaRequest req = makeRequest(QStringLiteral("tocancel"));
    const auto handle = service.request(
            req,
            [&](const MediaRequest &, const MediaResult &) { gotResult = true; },
            [&](const MediaRequest &, const Error &) { gotError = true; });

    service.cancel(handle);
    block.release();

    QTest::qWait(200);

    QVERIFY(!gotResult);
    QVERIFY(!gotError);
}

void TestThumbnailService::cancelAllExceptLeavesNamedHandles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    QSemaphore block(0);
    FakeRenderer renderer;
    renderer.blockOn = &block;

    ThumbnailService service(&cache, &renderer);
    service.setMaxConcurrency(1);

    bool gotA = false;
    bool gotB = false;

    const MediaRequest reqA = makeRequest(QStringLiteral("aaa"));
    const MediaRequest reqB = makeRequest(QStringLiteral("bbb"));

    const auto handleA = service.request(
            reqA,
            [&](const MediaRequest &, const MediaResult &) { gotA = true; },
            [](const MediaRequest &, const Error &) {});
    const auto handleB = service.request(
            reqB,
            [&](const MediaRequest &, const MediaResult &) { gotB = true; },
            [](const MediaRequest &, const Error &) {});

    service.cancelAllExcept({handleB});
    block.release(2);

    QTRY_VERIFY_WITH_TIMEOUT(gotB, 5000);
    QTest::qWait(100);

    QVERIFY(!gotA);
    QVERIFY(gotB);
}

void TestThumbnailService::renderErrorDeliversErrorCallback()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());
    FakeRenderer renderer;
    renderer.failNextRender = true;

    ThumbnailService service(&cache, &renderer);

    bool gotError = false;
    const MediaRequest req = makeRequest(QStringLiteral("badfile"));
    service.request(
            req,
            [&](const MediaRequest &, const MediaResult &) { QFAIL("unexpected result"); },
            [&](const MediaRequest &, const Error &err) {
                gotError = true;
                QCOMPARE(static_cast<int>(err.code()), static_cast<int>(ErrorCode::CorruptData));
            });

    QTRY_VERIFY_WITH_TIMEOUT(gotError, 5000);
}

void TestThumbnailService::interactivePriorityBeforeBackground()
{
    // Verify that an Interactive request is dequeued ahead of a Background
    // request when both are waiting.  A dedicated "blocker" request occupies
    // the single worker thread until we are ready to release, ensuring both
    // bg and fg are in the pending queue (not yet running) when we let them go.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ThumbnailDiskCache cache(tmp.path());

    // Individual semaphores give deterministic control over each render call.
    QSemaphore blockerGo(0);
    QSemaphore fgGo(0);
    QSemaphore bgGo(0);

    // A renderer whose behaviour is controlled per-request by index.
    // Render call 0 = blocker, call 1 = next (fg by priority), call 2 = last.
    struct SequencedRenderer final : ThumbnailRenderer
    {
        QSemaphore *sems[3]{};
        mutable int callIndex = 0;

        MediaResult render(const MediaRequest &request, Error *) const override
        {
            const int i = callIndex++;
            if (i < 3 && sems[i]) {
                sems[i]->acquire();
            }
            MediaResult r;
            r.bytes = QByteArray("T");
            r.format = QStringLiteral("jpeg");
            r.actualSize = request.targetSize;
            return r;
        }
    } renderer;
    renderer.sems[0] = &blockerGo;
    renderer.sems[1] = &fgGo;
    renderer.sems[2] = &bgGo;

    ThumbnailService service(&cache, &renderer);
    service.setMaxConcurrency(1);

    QStringList completionOrder;

    const MediaRequest blockerReq = makeRequest(QStringLiteral("blocker"), JobPriority::Opportunistic);
    const MediaRequest bgReq     = makeRequest(QStringLiteral("bg"),      JobPriority::Background);
    const MediaRequest fgReq     = makeRequest(QStringLiteral("fg"),      JobPriority::Interactive);

    // Submit the blocker first to occupy the single thread.
    service.request(
            blockerReq,
            [&](const MediaRequest &, const MediaResult &) {},
            [](const MediaRequest &, const Error &) {});

    // Give the pool time to dequeue the blocker (it is now blocking on blockerGo).
    QTest::qWait(50);

    // Submit bg and fg while the thread is busy — they go into the priority queue.
    service.request(
            bgReq,
            [&](const MediaRequest &r, const MediaResult &) {
                completionOrder.append(r.fingerprint.digest());
            },
            [](const MediaRequest &, const Error &) {});

    service.request(
            fgReq,
            [&](const MediaRequest &r, const MediaResult &) {
                completionOrder.append(r.fingerprint.digest());
            },
            [](const MediaRequest &, const Error &) {});

    // Release the blocker.  The thread will pick fg next (higher priority).
    blockerGo.release();
    fgGo.release();
    bgGo.release();

    QTRY_VERIFY_WITH_TIMEOUT(completionOrder.size() == 2, 5000);

    // fg must appear before bg in the completion order.
    const int fgIdx = completionOrder.indexOf(QStringLiteral("fg"));
    const int bgIdx = completionOrder.indexOf(QStringLiteral("bg"));
    QVERIFY(fgIdx >= 0 && bgIdx >= 0);
    QVERIFY(fgIdx < bgIdx);
}

QTEST_MAIN(TestThumbnailService)

#include "tst_thumbnail_service.moc"
