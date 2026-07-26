#include "pimio/browser/media_library_model.h"

#include "pimio/projection/projection_database.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/qtest_printers.h"
#include "pimio/testing/recording_media_request_service.h"

#include <QAbstractItemModelTester>
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
    void setVisibleRangeRequestsThumbnailsForWindow();
    void setVisibleRangeWithPrefetchExpandsWindow();
    void setVisibleRangeChangesCancelsPreviousRequests();
    void thumbnailResultTransitionsStatusToReady();
    void thumbnailErrorTransitionsStatusToError();
    void cancelledThumbnailResetsStatusToPending();
    void reloadClearsExistingItems();
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
    result.bytes = QByteArray("FAKE");
    result.format = QStringLiteral("jpeg");
    result.actualSize = QSize(160, 160);
    QVERIFY(service.complete(MediaRequestHandle(1), result));

    QCOMPARE(spy.size(), 1);
    const int status = model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt();
    QCOMPARE(status, static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready));
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
