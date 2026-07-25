#include "pimio/lore/lore_durable_store.h"
#include "pimio/projection/projection_database.h"

#include "lore_test_support.h"

#include "pimio/testing/qtest_printers.h"

#include <QFile>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace pimio::core;
using namespace pimio::lore;
using namespace pimio::projection;
using namespace pimio::testing;

namespace {

QList<QString> idValues(const QList<MediaId> &ids)
{
    QList<QString> values;
    values.reserve(ids.size());
    for (const MediaId &id : ids) {
        values.append(id.value());
    }
    std::sort(values.begin(), values.end());
    return values;
}

} // namespace

/// The projection against the real durable store rather than a fake.
///
/// `projection.rebuild` proves the projection's own logic. This proves the two
/// halves of the storage design actually fit: that the token LORE reports is
/// one the projection can act on, and that deleting the cache costs nothing.
class TestLoreProjection : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void deletingTheCacheReproducesIdenticalResultsFromLore();
    void aCommitMakesTheProjectionStale();
};

void TestLoreProjection::initTestCase()
{
    QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
             "the Qt SQLite driver must be present");
}

void TestLoreProjection::deletingTheCacheReproducesIdenticalResultsFromLore()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));
    const QString cachePath = temporary.filePath(QStringLiteral("projection.db"));

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    for (int index = 0; index < 25; ++index) {
        MediaRecord record = makeLoreRecord(QStringLiteral("item-%1").arg(index, 4, 10,
                                                                         QLatin1Char('0')),
                                            QStringLiteral("caption %1").arg(index));
        record.metadata.tags = QStringList{index % 2 == 0 ? QStringLiteral("even")
                                                          : QStringLiteral("odd")};
        QVERIFY(store.stage(record, &error));
    }
    QVERIFY2(store.commit(QStringLiteral("Import"), &error).has_value(),
             qPrintable(error.message()));

    QList<QString> ids;
    QList<QString> even;
    {
        ProjectionDatabase projection;
        QVERIFY2(projection.open(cachePath, &error), qPrintable(error.message()));
        QVERIFY(projection.isStale(store, &error));
        QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
        QVERIFY(!projection.isStale(store, &error));

        QCOMPARE(projection.recordCount(&error), 25);
        QCOMPARE(projection.projectedStateToken(&error), store.stateToken());
        ids = idValues(projection.listIds(&error));
        even = idValues(projection.idsWithTag(QStringLiteral("even"), &error));
        QCOMPARE(even.size(), 13);
        QCOMPARE(idValues(store.listIds(&error)), ids);
    }

    QVERIFY2(ProjectionDatabase::remove(cachePath, &error), qPrintable(error.message()));
    QVERIFY(!QFile::exists(cachePath));

    // The whole point of the design: the cache is disposable because the
    // durable store can answer every question it answered.
    ProjectionDatabase rebuilt;
    QVERIFY2(rebuilt.open(cachePath, &error), qPrintable(error.message()));
    QVERIFY(rebuilt.isStale(store, &error));
    QVERIFY2(rebuilt.rebuildFrom(store, &error), qPrintable(error.message()));

    QCOMPARE(idValues(rebuilt.listIds(&error)), ids);
    QCOMPARE(idValues(rebuilt.idsWithTag(QStringLiteral("even"), &error)), even);
    for (const QString &id : ids) {
        const auto durable = store.load(MediaId(id), &error);
        const auto projected = rebuilt.load(MediaId(id), &error);
        QVERIFY2(durable.has_value() && projected.has_value(), qPrintable(id));
        QCOMPARE(projected->metadata.caption, durable->metadata.caption);
    }
}

void TestLoreProjection::aCommitMakesTheProjectionStale()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString storePath = temporary.filePath(QStringLiteral("store"));

    LoreDurableStore store(storePath);
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("first"), QStringLiteral("one")), &error));
    QVERIFY2(store.commit(QStringLiteral("First"), &error).has_value(),
             qPrintable(error.message()));

    ProjectionDatabase projection;
    QVERIFY2(projection.openInMemory(&error), qPrintable(error.message()));
    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    QVERIFY(!projection.isStale(store, &error));

    // Staging is not a change to committed state, so it must not invalidate a
    // projection that is still an accurate view of what was committed.
    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("second"), QStringLiteral("two")), &error));
    QVERIFY(!projection.isStale(store, &error));

    QVERIFY2(store.commit(QStringLiteral("Second"), &error).has_value(),
             qPrintable(error.message()));
    QVERIFY(projection.isStale(store, &error));

    QVERIFY2(projection.rebuildFrom(store, &error), qPrintable(error.message()));
    QCOMPARE(projection.recordCount(&error), 2);
    QVERIFY(!projection.isStale(store, &error));
}

QTEST_MAIN(TestLoreProjection)

#include "tst_lore_projection.moc"
