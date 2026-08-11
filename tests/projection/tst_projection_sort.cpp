#include "pimio/projection/projection_database.h"

#include "pimio/core/metadata.h"
#include "pimio/core/types.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/qtest_printers.h"

#include <QObject>
#include <QTest>

using namespace pimio;
using namespace pimio::projection;

namespace {

const QDateTime kEpoch = QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC);

core::MediaId addRecord(testing::MemoryDurableStore &store, const core::MediaRecord &record)
{
    core::Error err;
    store.stage(record, &err);
    Q_ASSERT(!err.isError());
    store.commit(QStringLiteral("add"), &err);
    Q_ASSERT(!err.isError());
    return record.id;
}

/// A record with everything the sort columns read: name, size, file date and
/// capture time. \a id is spelled out so ties have a predictable order.
core::MediaRecord makeRecord(const QString &id, const QString &fileName, qint64 sizeBytes,
                             const QDateTime &lastModified, const QDateTime &captureTime)
{
    core::MediaRecord r;
    r.id = core::MediaId(id);
    r.fingerprint = core::ContentFingerprint(QStringLiteral("sha256"), id);
    r.identity.absolutePath = QStringLiteral("/lib/") + fileName;
    r.identity.volumeId = QStringLiteral("");
    r.identity.fileId = QStringLiteral("");
    r.identity.sizeBytes = sizeBytes;
    r.identity.lastModified = lastModified;
    r.metadata.fileName = fileName;
    r.metadata.folderPath = QStringLiteral("/lib");
    r.metadata.kind = core::MediaKind::Image;
    if (captureTime.isValid()) {
        r.metadata.captureTime = core::CaptureTime::fromLocalWallClock(captureTime);
    }
    r.metadata.captureTimeOrigin = core::MetadataOrigin::Embedded;
    r.metadata.cameraMake = QStringLiteral("");
    r.metadata.cameraModel = QStringLiteral("");
    r.metadata.caption = QStringLiteral("");
    return r;
}

QDateTime day(int d)
{
    return QDateTime(QDate(2024, 4, d), QTime(9, 0, 0), Qt::UTC);
}

/// Names in the order \a ids appear, so failures read as file names.
QStringList namesOf(const QList<core::MediaId> &ids)
{
    QStringList names;
    for (const core::MediaId &id : ids) {
        names << id.value();
    }
    return names;
}

} // namespace

class TestProjectionSort : public QObject
{
    Q_OBJECT

private:
    /// Three records that disagree on every sort column, so each key produces
    /// a different order and no test can pass by accident.
    static void populate(testing::MemoryDurableStore &store)
    {
        // name        size   file date  capture
        // beta.png    300    day 3      day 2
        // Alpha.jpg   100    day 1      day 3
        // gamma.jpeg  200    day 2      day 1
        addRecord(store, makeRecord(QStringLiteral("beta.png"), QStringLiteral("beta.png"), 300,
                                    day(3), day(2)));
        addRecord(store, makeRecord(QStringLiteral("Alpha.jpg"), QStringLiteral("Alpha.jpg"), 100,
                                    day(1), day(3)));
        addRecord(store, makeRecord(QStringLiteral("gamma.jpeg"), QStringLiteral("gamma.jpeg"),
                                    200, day(2), day(1)));
    }

    static void openAndFill(ProjectionDatabase &db, testing::MemoryDurableStore &store)
    {
        core::Error err;
        QVERIFY(db.openInMemory(&err));
        QVERIFY(db.rebuildFrom(store, &err));
        QVERIFY(!err.isError());
    }

private slots:
    void sortKeysOrderIndependently_data()
    {
        QTest::addColumn<int>("sortKey");
        QTest::addColumn<QStringList>("ascending");

        QTest::newRow("capture time")
                << int(ProjectionDatabase::SortKey::CaptureTime)
                << QStringList{QStringLiteral("gamma.jpeg"), QStringLiteral("beta.png"),
                               QStringLiteral("Alpha.jpg")};
        // Case-insensitive: "Alpha" sorts before "beta" despite the capital.
        QTest::newRow("file name")
                << int(ProjectionDatabase::SortKey::FileName)
                << QStringList{QStringLiteral("Alpha.jpg"), QStringLiteral("beta.png"),
                               QStringLiteral("gamma.jpeg")};
        QTest::newRow("file date")
                << int(ProjectionDatabase::SortKey::FileDate)
                << QStringList{QStringLiteral("Alpha.jpg"), QStringLiteral("gamma.jpeg"),
                               QStringLiteral("beta.png")};
        // jpeg < jpg < png, and name breaks ties inside one extension.
        QTest::newRow("file type")
                << int(ProjectionDatabase::SortKey::FileType)
                << QStringList{QStringLiteral("gamma.jpeg"), QStringLiteral("Alpha.jpg"),
                               QStringLiteral("beta.png")};
        QTest::newRow("file size")
                << int(ProjectionDatabase::SortKey::FileSize)
                << QStringList{QStringLiteral("Alpha.jpg"), QStringLiteral("gamma.jpeg"),
                               QStringLiteral("beta.png")};
    }

    void sortKeysOrderIndependently()
    {
        QFETCH(int, sortKey);
        QFETCH(QStringList, ascending);

        testing::FakeClock clock(kEpoch);
        testing::MemoryDurableStore store(clock);
        populate(store);

        ProjectionDatabase db;
        openAndFill(db, store);

        core::Error err;
        const auto key = static_cast<ProjectionDatabase::SortKey>(sortKey);
        QCOMPARE(namesOf(db.idsSorted(key, Qt::AscendingOrder, &err)), ascending);
        QVERIFY(!err.isError());

        QStringList descending = ascending;
        std::reverse(descending.begin(), descending.end());
        QCOMPARE(namesOf(db.idsSorted(key, Qt::DescendingOrder, &err)), descending);
        QVERIFY(!err.isError());
    }

    void tiesKeepAscendingIdOrderInBothDirections()
    {
        testing::FakeClock clock(kEpoch);
        testing::MemoryDurableStore store(clock);
        // Same size, same name: only the id can break the tie.
        addRecord(store, makeRecord(QStringLiteral("id-aaa"), QStringLiteral("same.jpg"), 100,
                                    day(1), day(1)));
        addRecord(store, makeRecord(QStringLiteral("id-zzz"), QStringLiteral("same.jpg"), 100,
                                    day(1), day(1)));

        ProjectionDatabase db;
        openAndFill(db, store);

        core::Error err;
        const QStringList expected{QStringLiteral("id-aaa"), QStringLiteral("id-zzz")};
        QCOMPARE(namesOf(db.idsSorted(ProjectionDatabase::SortKey::FileSize,
                                      Qt::AscendingOrder, &err)),
                 expected);
        QCOMPARE(namesOf(db.idsSorted(ProjectionDatabase::SortKey::FileSize,
                                      Qt::DescendingOrder, &err)),
                 expected);
        QVERIFY(!err.isError());
    }

    void recordWithoutAFileDateStillAppears()
    {
        testing::FakeClock clock(kEpoch);
        testing::MemoryDurableStore store(clock);
        addRecord(store, makeRecord(QStringLiteral("dated"), QStringLiteral("dated.jpg"), 100,
                                    day(5), day(5)));
        addRecord(store, makeRecord(QStringLiteral("undated"), QStringLiteral("undated.jpg"), 100,
                                    QDateTime(), day(5)));

        ProjectionDatabase db;
        openAndFill(db, store);

        core::Error err;
        // The unknown date sorts with the oldest rather than dropping out.
        QCOMPARE(namesOf(db.idsSorted(ProjectionDatabase::SortKey::FileDate,
                                      Qt::AscendingOrder, &err)),
                 (QStringList{QStringLiteral("undated"), QStringLiteral("dated")}));
        QVERIFY(!err.isError());
    }

    void filesWithoutAnExtensionSortTogetherFirst()
    {
        testing::FakeClock clock(kEpoch);
        testing::MemoryDurableStore store(clock);
        addRecord(store, makeRecord(QStringLiteral("plain"), QStringLiteral("README"), 100, day(1),
                                    day(1)));
        // A leading dot is a hidden file, not an extension.
        addRecord(store, makeRecord(QStringLiteral("hidden"), QStringLiteral(".hidden"), 100,
                                    day(1), day(1)));
        addRecord(store, makeRecord(QStringLiteral("image"), QStringLiteral("photo.JPG"), 100,
                                    day(1), day(1)));

        ProjectionDatabase db;
        openAndFill(db, store);

        core::Error err;
        // ".hidden" and "README" have no extension, so the name decides; the
        // extension is compared lower-cased, so ".JPG" groups with ".jpg".
        QCOMPARE(namesOf(db.idsSorted(ProjectionDatabase::SortKey::FileType,
                                      Qt::AscendingOrder, &err)),
                 (QStringList{QStringLiteral("hidden"), QStringLiteral("plain"),
                              QStringLiteral("image")}));
        QVERIFY(!err.isError());
    }
};

QTEST_MAIN(TestProjectionSort)

#include "tst_projection_sort.moc"
