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
