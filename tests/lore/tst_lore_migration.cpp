#include "lore_test_support.h"

#include "pimio/lore/lore_durable_store.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace pimio::core;
using namespace pimio::lore;
using namespace pimio::testing;

class TestLoreMigration : public QObject
{
    Q_OBJECT

private slots:
    void opensAndWritesRepositoryCreatedByLore085();
};

void TestLoreMigration::opensAndWritesRepositoryCreatedByLore085()
{
    PIMIO_SKIP_WITHOUT_LORE();

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    QProcess extract;
    extract.setWorkingDirectory(temporary.path());
    extract.start(QStringLiteral(PIMIO_CMAKE_COMMAND),
                  {QStringLiteral("-E"), QStringLiteral("tar"), QStringLiteral("xzf"),
                   QStringLiteral(PIMIO_LORE_085_FIXTURE_PATH)});
    QVERIFY(extract.waitForStarted(30'000));
    QVERIFY(extract.waitForFinished(30'000));
    QVERIFY2(extract.exitStatus() == QProcess::NormalExit && extract.exitCode() == 0,
             extract.readAllStandardError().constData());

    LoreDurableStore store(temporary.filePath(QStringLiteral("store")));
    Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message()));

    const auto descriptor = store.libraryDescriptor(&error);
    QVERIFY2(descriptor.has_value(), qPrintable(error.message()));
    QCOMPARE(descriptor->id, QStringLiteral("fixture-library-085"));
    QCOMPARE(store.history(-1, &error).size(), 1);

    const auto existing = store.load(MediaId(QStringLiteral("fixture-record")), &error);
    QVERIFY2(existing.has_value(), qPrintable(error.message()));
    QCOMPARE(existing->metadata.caption, QStringLiteral("written by LORE 0.8.5"));

    QVERIFY(store.stage(makeLoreRecord(QStringLiteral("post-migration"),
                                      QStringLiteral("written by LORE 0.9.0")),
                        &error));
    QVERIFY2(store.commit(QStringLiteral("Migration write"), &error).has_value(),
             qPrintable(error.message()));
    QCOMPARE(store.history(-1, &error).size(), 2);
    QVERIFY(store.load(MediaId(QStringLiteral("post-migration")), &error).has_value());
}

QTEST_GUILESS_MAIN(TestLoreMigration)

#include "tst_lore_migration.moc"
