#include "pimio/browser/media_library_model.h"

#include "pimio/browser/thumbnail_image_provider.h"

#include "pimio/projection/projection_database.h"
#include "pimio/testing/fake_clock.h"
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

namespace {

MediaRecord makeRecord(const QString &id, qint64 captureMSecs,
                       const QString &digest = QString())
{
    MediaRecord record;
    record.id = MediaId(id);
    record.fingerprint = ContentFingerprint(
            QStringLiteral("sha256"), digest.isEmpty() ? id : digest);
    record.identity.absolutePath = QStringLiteral("/library/%1.jpg").arg(id);
    record.identity.volumeId = QStringLiteral("vol-1");
    record.identity.fileId = id;
    record.identity.sizeBytes = 4096;
    record.identity.lastModified = QDateTime::fromMSecsSinceEpoch(captureMSecs, Qt::UTC);

    record.metadata.kind = MediaKind::Image;
    record.metadata.fileName = id + QStringLiteral(".jpg");
    record.metadata.folderPath = QStringLiteral("/library");
    record.metadata.captureTime = CaptureTime::fromOffset(
            QDateTime::fromMSecsSinceEpoch(captureMSecs, Qt::UTC), 0);
    record.metadata.captureTimeOrigin = MetadataOrigin::Embedded;
    record.metadata.cameraMake = QStringLiteral("TestCam");
    record.metadata.cameraModel = QStringLiteral("Model X");
    record.metadata.lensModel = QStringLiteral("Lens 1");
    record.metadata.pixelWidth = 1920;
    record.metadata.pixelHeight = 1080;
    record.metadata.rotationDegrees = 0;
    record.metadata.durationMs = 0;
    record.metadata.hasAudio = false;
    record.metadata.rating = 0;
    record.metadata.caption = QStringLiteral("");
    return record;
}

} // namespace

class TestBrowserModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rowCountMatchesProjectionSize();
    void emptyProjectionYieldsZeroRows();
    void mediaIdRoleReturnsStableId();
    void absolutePathRoleReturnsPath();
    void captureTimeStringRoleReturnsIsoString();
    void mediaKindRoleReturnsImageForImages();
    void thumbnailStatusStartsAsPending();
    void itemAtReturnsDetailRoles();
    void setVisibleRangeRequestsThumbnailsForWindow();
    void visibleRangeIsInvokableFromQml();
    void setVisibleRangeWithPrefetchExpandsWindow();
    void setVisibleRangeChangesCancelsPreviousRequests();
    void thumbnailResultTransitionsStatusToReady();
    void invalidThumbnailResultTransitionsStatusToError();
    void duplicateContentRequestsCompleteIndependently();
    void thumbnailErrorTransitionsStatusToError();
    void thumbnailResultIsPushedToTheImageProvider();
    void cancelledThumbnailResetsStatusToPending();
    void reloadClearsExistingItems();
    void setSortingReordersRows();
    void unknownSortKeyKeepsTheCurrentOrder();
    void tilePixelSizeSelectsAThumbnailTier();
    void changingTheThumbnailSizeReRequestsTheVisibleWindow();
    void modelPassesGenericModelTest();

private:
    // Builds an in-memory projection with \a count items and leaves it in m_db.
    void populate(int count);

    FakeClock m_clock{QDateTime::fromMSecsSinceEpoch(0, Qt::UTC)};
    std::unique_ptr<ProjectionDatabase> m_db;
};

void TestBrowserModel::initTestCase()
{
    QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
             "the Qt SQLite driver must be present");
}

void TestBrowserModel::populate(int count)
{
    MemoryDurableStore store(m_clock);
    Error error;
    for (int i = 0; i < count; ++i) {
        const QString id = QStringLiteral("item%1").arg(i, 3, 10, QLatin1Char('0'));
        QVERIFY(store.stage(makeRecord(id, static_cast<qint64>(i) * 1000), &error));
    }
    if (count > 0) {
        QVERIFY(store.commit(QStringLiteral("test"), &error).has_value());
    }

    m_db = std::make_unique<ProjectionDatabase>();
    QVERIFY(m_db->openInMemory(&error));
    QVERIFY2(m_db->rebuildFrom(store, &error), qPrintable(error.message()));
}

void TestBrowserModel::rowCountMatchesProjectionSize()
{
    populate(5);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());
    QCOMPARE(model.rowCount(), 5);
}

void TestBrowserModel::emptyProjectionYieldsZeroRows()
{
    populate(0);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());
    QCOMPARE(model.rowCount(), 0);
}

void TestBrowserModel::mediaIdRoleReturnsStableId()
{
    populate(3);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    const QVariant id = model.data(model.index(0), MediaLibraryModel::MediaIdRole);
    QVERIFY(!id.toString().isEmpty());
}

void TestBrowserModel::absolutePathRoleReturnsPath()
{
    populate(1);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    const QString path = model.data(model.index(0), MediaLibraryModel::AbsolutePathRole).toString();
    QVERIFY(path.startsWith(QStringLiteral("/library/")));
    QVERIFY(path.endsWith(QStringLiteral(".jpg")));
}

void TestBrowserModel::captureTimeStringRoleReturnsIsoString()
{
    populate(1);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    const QString t = model.data(model.index(0), MediaLibraryModel::CaptureTimeStringRole).toString();
    QVERIFY(!t.isEmpty());
    QVERIFY(t.contains(QLatin1Char('T')));
}

void TestBrowserModel::mediaKindRoleReturnsImageForImages()
{
    populate(1);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    const int kind = model.data(model.index(0), MediaLibraryModel::MediaKindRole).toInt();
    QCOMPARE(kind, static_cast<int>(MediaKind::Image));
}

void TestBrowserModel::thumbnailStatusStartsAsPending()
{
    populate(3);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    for (int i = 0; i < 3; ++i) {
        const int status = model.data(model.index(i), MediaLibraryModel::ThumbnailStatusRole).toInt();
        QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Pending));
    }
}

void TestBrowserModel::itemAtReturnsDetailRoles()
{
    populate(1);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    const QVariantMap item = model.itemAt(0);
    QVERIFY(!item.value(QStringLiteral("mediaId")).toString().isEmpty());
    QCOMPARE(item.value(QStringLiteral("absolutePath")).toString(),
             QStringLiteral("/library/item000.jpg"));
    QCOMPARE(item.value(QStringLiteral("mediaKind")).toInt(),
             static_cast<int>(MediaKind::Image));
    QCOMPARE(item.value(QStringLiteral("thumbnailStatus")).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Pending));
    QVERIFY(model.itemAt(-1).isEmpty());
}

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

void TestBrowserModel::reloadClearsExistingItems()
{
    populate(5);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    QCOMPARE(model.rowCount(), 5);

    // Rebuild the projection with fewer items.
    MemoryDurableStore store(m_clock);
    Error error;
    QVERIFY(store.stage(makeRecord(QStringLiteral("only"), 1000), &error));
    QVERIFY(store.commit(QStringLiteral("reduced"), &error).has_value());
    QVERIFY(m_db->rebuildFrom(store, &error));

    model.reload();
    QCOMPARE(model.rowCount(), 1);
}

void TestBrowserModel::setSortingReordersRows()
{
    populate(3);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());

    const auto idAt = [&model](int row) {
        return model.data(model.index(row), MediaLibraryModel::MediaIdRole).toString();
    };
    QCOMPARE(idAt(0), QStringLiteral("item000"));

    // Descending by file name reverses item000..item002.
    model.setSorting(static_cast<int>(ProjectionDatabase::SortKey::FileName), true);
    QCOMPARE(model.sortKey(), ProjectionDatabase::SortKey::FileName);
    QCOMPARE(model.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(idAt(0), QStringLiteral("item002"));
    QCOMPARE(idAt(2), QStringLiteral("item000"));

    model.setSorting(static_cast<int>(ProjectionDatabase::SortKey::FileName), false);
    QCOMPARE(idAt(0), QStringLiteral("item000"));
}

void TestBrowserModel::unknownSortKeyKeepsTheCurrentOrder()
{
    populate(3);
    MediaLibraryModel model;
    model.setDatabase(m_db.get());
    model.setSorting(static_cast<int>(ProjectionDatabase::SortKey::FileSize), false);

    // A value no build of pimio knows: the view keeps working.
    model.setSorting(9999, false);
    QCOMPARE(model.sortKey(), ProjectionDatabase::SortKey::FileSize);
    QCOMPARE(model.rowCount(), 3);
}

void TestBrowserModel::tilePixelSizeSelectsAThumbnailTier()
{
    const QList<int> tiers = MediaLibraryModel::thumbnailTiers();
    QVERIFY(!tiers.isEmpty());

    MediaLibraryModel model;
    // The smallest tier that still covers the tile, so a thumbnail is never
    // upscaled into the grid.
    model.setTilePixelSize(96);
    QCOMPARE(model.thumbnailSize(), QSize(tiers.constFirst(), tiers.constFirst()));
    model.setTilePixelSize(tiers.constFirst() + 1);
    QCOMPARE(model.thumbnailSize(), QSize(tiers.at(1), tiers.at(1)));
    // Beyond the largest tier the largest is used rather than an unbounded
    // decode; the tile slider is capped so this cannot happen from the UI.
    model.setTilePixelSize(4096);
    QCOMPARE(model.thumbnailSize(), QSize(tiers.constLast(), tiers.constLast()));
}

void TestBrowserModel::changingTheThumbnailSizeReRequestsTheVisibleWindow()
{
    populate(10);
    RecordingMediaRequestService service;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.setTilePixelSize(96);
    model.setVisibleRange(2, 4);
    QCOMPARE(service.requestedCacheKeys().size(), 3);

    // A bigger tile needs bigger thumbnails: the rows on screen go back to
    // Pending and are requested again at the new size.
    model.setTilePixelSize(512);
    QCOMPARE(service.requestedCacheKeys().size(), 6);
    for (int row = 2; row <= 4; ++row) {
        const int status =
                model.data(model.index(row), MediaLibraryModel::ThumbnailStatusRole).toInt();
        QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Loading));
    }
    // The second batch asks for different cache keys than the first.
    const QStringList keys = service.requestedCacheKeys();
    QVERIFY(keys.mid(0, 3) != keys.mid(3, 3));
}

void TestBrowserModel::modelPassesGenericModelTest()
{
    populate(4);
    auto model = std::make_unique<MediaLibraryModel>();
    model->setDatabase(m_db.get());

    // QAbstractItemModelTester verifies basic model contract invariants on
    // construction and during any model mutations via Qt's own test harness.
    QAbstractItemModelTester{model.get(),
                             QAbstractItemModelTester::FailureReportingMode::Fatal};
}

QTEST_MAIN(TestBrowserModel)

#include "tst_browser_model.moc"
