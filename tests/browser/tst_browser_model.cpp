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

namespace {

/// A small encoded PNG, as a completed thumbnail request would carry.
MediaResult makeThumbnailResult()
{
    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::green);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    source.save(&buffer, "png");

    MediaResult result;
    result.bytes = encoded;
    result.format = QStringLiteral("png");
    result.actualSize = source.size();
    return result;
}

} // namespace

void TestBrowserModel::thumbnailsBeyondTheRetentionBoundAreDroppedAndRequestedAgain()
{
    // The bug this covers: a long scroll used to leave the model claiming
    // rows were Ready after their images had been evicted from the provider's
    // cache, so QML asked for image://thumbnail/<id>, got nothing, and the
    // tile stayed grey for the rest of the session.
    populate(30);
    RecordingMediaRequestService service;
    ThumbnailImageProvider provider;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setRetainedThumbnailLimit(4);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.setImageProvider(&provider);

    QStringList mediaIds;
    for (int row = 0; row < 30; ++row) {
        mediaIds.append(model.data(model.index(row), MediaLibraryModel::MediaIdRole).toString());
    }

    // Scroll the whole library one row at a time, letting each render finish.
    for (int row = 0; row < 30; ++row) {
        model.setVisibleRange(row, row);
        QVERIFY(service.complete(MediaRequestHandle(static_cast<quint64>(row) + 1),
                                 makeThumbnailResult()));
    }

    // Something was dropped: the model does not hold all thirty.
    QVERIFY(model.retainedThumbnailCount() < 30);

    // Whatever the model still calls Ready, the provider can still serve.
    for (int row = 0; row < 30; ++row) {
        const int status =
                model.data(model.index(row), MediaLibraryModel::ThumbnailStatusRole).toInt();
        if (status == static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready)) {
            QVERIFY2(provider.contains(mediaIds.at(row)),
                     qPrintable(QStringLiteral("row %1 says Ready but the provider has no image "
                                               "for it").arg(row)));
        }
    }

    // The earliest rows were dropped rather than left claiming a thumbnail...
    QCOMPARE(model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Pending));
    QVERIFY(!provider.contains(mediaIds.constFirst()));

    // ...and scrolling back to them asks for them again.
    const int requestsBefore = service.requestedCacheKeys().size();
    model.setVisibleRange(0, 0);
    QVERIFY(service.requestedCacheKeys().size() > requestsBefore);
    QCOMPARE(model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Loading));
}

void TestBrowserModel::refreshThumbnailReRequestsARowTheProviderCannotServe()
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
    QVERIFY(service.complete(MediaRequestHandle(1), makeThumbnailResult()));
    QCOMPARE(model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready));

    // The view reports that image://thumbnail/<id> failed anyway.
    model.refreshThumbnail(0);

    QCOMPARE(model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Loading));
    QCOMPARE(service.requestedCacheKeys().size(), 2);
}

void TestBrowserModel::appendingReloadKeepsLoadedThumbnailsAndInsertsRows()
{
    // A scan in progress reloads the model every time it commits a batch. The
    // rows it already showed must keep their pictures and their place.
    populate(2);
    RecordingMediaRequestService service;
    ThumbnailImageProvider provider;

    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.setImageProvider(&provider);

    model.setVisibleRange(0, 0);
    QVERIFY(service.complete(MediaRequestHandle(1), makeThumbnailResult()));
    const QString firstId =
            model.data(model.index(0), MediaLibraryModel::MediaIdRole).toString();

    // The scan finds two more files, sorting after the ones already shown.
    MemoryDurableStore store(m_clock);
    Error error;
    for (int i = 0; i < 4; ++i) {
        const QString id = QStringLiteral("item%1").arg(i, 3, 10, QLatin1Char('0'));
        QVERIFY(store.stage(makeRecord(id, static_cast<qint64>(i) * 1000), &error));
    }
    QVERIFY(store.commit(QStringLiteral("more"), &error).has_value());
    QVERIFY(m_db->rebuildFrom(store, &error));

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    model.reload();

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertSpy.count(), 1);
    QCOMPARE(model.data(model.index(0), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready));
    QVERIFY(provider.contains(firstId));
}

void TestBrowserModel::insertingReloadKeepsLoadedThumbnailsAndInsertsRows()
{
    // Scan batches arrive in filesystem order, not sort order. New rows can
    // therefore belong before and between rows already shown without changing
    // the relative order of anything the user was looking at.
    MemoryDurableStore initialStore(m_clock);
    Error error;
    QVERIFY(initialStore.stage(makeRecord(QStringLiteral("b"), 2000), &error));
    QVERIFY(initialStore.stage(makeRecord(QStringLiteral("d"), 4000), &error));
    QVERIFY(initialStore.commit(QStringLiteral("initial"), &error).has_value());

    m_db = std::make_unique<ProjectionDatabase>();
    QVERIFY(m_db->openInMemory(&error));
    QVERIFY(m_db->rebuildFrom(initialStore, &error));

    RecordingMediaRequestService service;
    ThumbnailImageProvider provider;
    MediaLibraryModel model;
    model.setPrefetchMargin(0);
    model.setDatabase(m_db.get());
    model.setRequestService(&service);
    model.setImageProvider(&provider);
    QAbstractItemModelTester tester(&model,
                                    QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setVisibleRange(0, 0);
    QVERIFY(service.complete(MediaRequestHandle(1), makeThumbnailResult()));
    const QString retainedId =
            model.data(model.index(0), MediaLibraryModel::MediaIdRole).toString();

    MemoryDurableStore expandedStore(m_clock);
    const QList<QPair<QString, qint64>> records{
        {QStringLiteral("a"), 1000},
        {QStringLiteral("b"), 2000},
        {QStringLiteral("c"), 3000},
        {QStringLiteral("d"), 4000},
        {QStringLiteral("e"), 5000},
    };
    for (const auto &[id, captureTime] : records) {
        QVERIFY(expandedStore.stage(makeRecord(id, captureTime), &error));
    }
    QVERIFY(expandedStore.commit(QStringLiteral("expanded"), &error).has_value());
    QVERIFY(m_db->rebuildFrom(expandedStore, &error));

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    model.reload();

    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertSpy.count(), 3);
    QCOMPARE(insertSpy.at(0).at(1).toInt(), 0);
    QCOMPARE(insertSpy.at(1).at(1).toInt(), 2);
    QCOMPARE(insertSpy.at(2).at(1).toInt(), 4);

    QStringList orderedIds;
    for (int row = 0; row < model.rowCount(); ++row) {
        orderedIds.append(model.data(model.index(row), MediaLibraryModel::MediaIdRole).toString());
    }
    QCOMPARE(orderedIds, QStringList({QStringLiteral("a"), QStringLiteral("b"),
                                      QStringLiteral("c"), QStringLiteral("d"),
                                      QStringLiteral("e")}));
    QCOMPARE(model.data(model.index(1), MediaLibraryModel::ThumbnailStatusRole).toInt(),
             static_cast<int>(MediaLibraryModel::ThumbnailStatus::Ready));
    QVERIFY(provider.contains(retainedId));
}

QTEST_MAIN(TestBrowserModel)
