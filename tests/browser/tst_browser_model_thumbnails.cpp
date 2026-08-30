#include "tst_browser_model_fixture.h"

#include "pimio/browser/media_library_model.h"

#include "browser_test_support.h"
#include "pimio/browser/thumbnail_image_provider.h"

#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/qtest_printers.h"
#include "pimio/testing/recording_media_request_service.h"

#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTest>

using namespace pimio::browser;
using namespace pimio::core;
using namespace pimio::projection;
using namespace pimio::testing;
using pimio::tests::browser_support::makeRecord;

void TestBrowserModel::setVisibleRangeRequestsThumbnailsForWindow()
{
    populate(10);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    // Show rows 2–4 (three items).
    model.setVisibleRange(2, 4);

    QCOMPARE(service.requestedCacheKeys().size(), 3);
    QCOMPARE(service.pendingCacheKeys().size(), 3);

    // All three items should now be in Loading state.
    for (int row = 2; row <= 4; ++row) {
        const int status = model.data(model.index(row), MediaLibraryModel::ThumbnailStatusRole).toInt();
        QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Loading));
    }
}

void TestBrowserModel::visibleRangeIsInvokableFromQml()
{
    populate(10);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    QVERIFY(QMetaObject::invokeMethod(&model, "setVisibleRange", Q_ARG(int, 2), Q_ARG(int, 4)));
    QCOMPARE(service.requestedCacheKeys().size(), 3);
}

void TestBrowserModel::setVisibleRangeWithPrefetchExpandsWindow()
{
    populate(10);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(2);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    // Visible 3–5, margin 2 → window [1, 7] (7 requests).
    model.setVisibleRange(3, 5);

    QCOMPARE(service.requestedCacheKeys().size(), 7);
}

void TestBrowserModel::setVisibleRangeChangesCancelsPreviousRequests()
{
    populate(20);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    // First visible window: rows 0–4 (5 requests).
    model.setVisibleRange(0, 4);
    QCOMPARE(service.pendingCacheKeys().size(), 5);
    QCOMPARE(service.cancelledCount(), 0);

    // Scroll to rows 10–14: old 5 requests should be cancelled.
    model.setVisibleRange(10, 14);
    QCOMPARE(service.cancelledCount(), 5);
    QCOMPARE(service.pendingCacheKeys().size(), 5);
}

void TestBrowserModel::thumbnailResultTransitionsStatusToReady()
{
    populate(3);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    QSignalSpy spy(&model, &MediaLibraryModel::dataChanged);

    model.setVisibleRange(0, 0);
    QCOMPARE(service.pendingCacheKeys().size(), 1);

    // Simulate a successful result for row 0.
    // RecordingMediaRequestService assigns handles starting at 1.
    MediaResult result;
    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::green);
    QBuffer encoded(&result.bytes);
    encoded.open(QIODevice::WriteOnly);
    QVERIFY(source.save(&encoded, "png"));
    result.format = QStringLiteral("jpeg");
    result.actualSize = QSize(160, 160);
    QVERIFY(service.complete(MediaRequestHandle(1), result));

    QCOMPARE(spy.size(), 1);
    const int status = model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt();
    QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready));
}

void TestBrowserModel::invalidThumbnailResultTransitionsStatusToError()
{
    populate(1);
    RecordingMediaRequestService service;
    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.requestThumbnail(0);

    MediaResult result;
    result.bytes = QByteArrayLiteral("not an image");
    QVERIFY(service.complete(MediaRequestHandle(1), result));

    QCOMPARE(model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Error));
}

void TestBrowserModel::duplicateContentRequestsCompleteIndependently()
{
    MemoryDurableStore store(m_clock);
    Error error;
    QVERIFY(store.stage(makeRecord(QStringLiteral("first"), 1000, QStringLiteral("same")),
                        &error));
    QVERIFY(store.stage(makeRecord(QStringLiteral("second"), 2000, QStringLiteral("same")),
                        &error));
    QVERIFY(store.commit(QStringLiteral("test"), &error).has_value());

    m_db = std::make_unique<ProjectionDatabase>();
    QVERIFY(m_db->openInMemory(&error));
    QVERIFY2(m_db->rebuildFrom(store, &error), qPrintable(error.message()));

    RecordingMediaRequestService service;
    MediaLibraryModel model;
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.setPrefetchMargin(0);
    model.setVisibleRange(0, 1);

    QCOMPARE(service.requestedCacheKeys().size(), 2);
    MediaResult result;
    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::green);
    QBuffer encoded(&result.bytes);
    encoded.open(QIODevice::WriteOnly);
    QVERIFY(source.save(&encoded, "png"));
    QVERIFY(service.complete(MediaRequestHandle(1), result));
    QVERIFY(service.complete(MediaRequestHandle(2), result));

    for (int row = 0; row < 2; ++row) {
        QCOMPARE(model.data(model.index(row), MediaLibraryModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready));
    }
}

void TestBrowserModel::thumbnailResultIsPushedToTheImageProvider()
{
    populate(1);
    RecordingMediaRequestService service;
    ThumbnailImageProvider provider;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.setImageProvider(&provider);

    model.setVisibleRange(0, 0);
    QCOMPARE(service.pendingCacheKeys().size(), 1);

    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::green);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(source.save(&buffer, "png"));

    MediaResult result;
    result.bytes = encoded;
    result.format = QStringLiteral("png");
    result.actualSize = source.size();
    QVERIFY(service.complete(MediaRequestHandle(1), result));

    const QString mediaId = model.data(model.index(0), MediaLibraryModel::MediaIdRole).toString();
    const QImage served = provider.requestImage(mediaId, nullptr, QSize());
    QVERIFY2(!served.isNull(), "Expected the decoded thumbnail to be retrievable via the image "
                              "provider under its mediaId");
    QCOMPARE(served.size(), source.size());
}

void TestBrowserModel::thumbnailErrorTransitionsStatusToError()
{
    populate(3);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    QSignalSpy spy(&model, &MediaLibraryModel::dataChanged);

    model.setVisibleRange(1, 1);
    QCOMPARE(service.pendingCacheKeys().size(), 1);

    QVERIFY(service.fail(MediaRequestHandle(1), Error(ErrorCode::CorruptData, {})));

    QCOMPARE(spy.size(), 1);
    const int status = model.data(model.index(1), MediaLibraryModel::ThumbnailStatusRole).toInt();
    QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Error));
}

void TestBrowserModel::cancelledThumbnailResetsStatusToPending()
{
    populate(10);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);

    // Request rows 0–2.
    model.setVisibleRange(0, 2);
    QCOMPARE(service.pendingCacheKeys().size(), 3);

    // Scroll away from row 0: it should become Pending again.
    model.setVisibleRange(5, 7);

    const int status = model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt();
    QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Pending));
}

