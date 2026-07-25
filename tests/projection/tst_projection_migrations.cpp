#include "pimio/projection/migration.h"

#include "pimio/testing/qtest_printers.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace pimio::core;
using namespace pimio::projection;

namespace {

/// A connection that removes itself, so a failing test cannot leak a handle
/// into the next one.
class Connection
{
public:
    explicit Connection(const QString &path)
        : m_name(QStringLiteral("tst-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        database.setDatabaseName(path);
        m_opened = database.open();
    }

    ~Connection()
    {
        {
            QSqlDatabase database = QSqlDatabase::database(m_name, false);
            if (database.isValid()) {
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(m_name);
    }

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    bool isOpen() const { return m_opened; }
    QSqlDatabase database() const { return QSqlDatabase::database(m_name, false); }

private:
    QString m_name;
    bool m_opened = false;
};

/// Synthetic migrations. The runner is a general mechanism, so it is tested
/// with migrations that exist only for the test rather than by inventing
/// product schema changes that nothing needs.
QList<Migration> threeGoodMigrations()
{
    return {
        Migration{1, QStringLiteral("create-widget"),
                  {QStringLiteral("CREATE TABLE widget (id INTEGER PRIMARY KEY, name TEXT)")}},
        Migration{2, QStringLiteral("add-colour"),
                  {QStringLiteral("ALTER TABLE widget ADD COLUMN colour TEXT NOT NULL DEFAULT ''"),
                   QStringLiteral("CREATE INDEX widget_colour ON widget(colour)")}},
        Migration{3, QStringLiteral("create-gadget"),
                  {QStringLiteral("CREATE TABLE gadget (id INTEGER PRIMARY KEY)")}},
    };
}

bool tableExists(QSqlDatabase database, const QString &name)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND "
                                 "name = ?"));
    query.addBindValue(name);
    return query.exec() && query.next() && query.value(0).toInt() == 1;
}

} // namespace

class TestProjectionMigrations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void emptyDatabaseMigratesToTheLatestVersion();
    void reopeningACurrentDatabaseChangesNothing();
    void aPreviousVersionIsUpgradedWithoutLosingData();
    void aFailedMigrationRollsBackAndKeepsThePreviousVersion();
    void aKilledMigrationLeavesTheDatabaseAtThePreviousVersion();
    void aNewerDatabaseIsRefusedRatherThanDowngraded();
    void nonContiguousVersionsAreRejected();

private:
    QString m_helperPath;
};

void TestProjectionMigrations::initTestCase()
{
    QVERIFY2(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
             "the Qt SQLite driver must be present");
    m_helperPath = QCoreApplication::applicationDirPath()
                   + QStringLiteral("/pimio_projection_migration_helper");
#ifdef Q_OS_WIN
    m_helperPath += QStringLiteral(".exe");
#endif
}

void TestProjectionMigrations::emptyDatabaseMigratesToTheLatestVersion()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    Connection connection(temporary.filePath(QStringLiteral("empty.db")));
    QVERIFY(connection.isOpen());
    QSqlDatabase database = connection.database();

    Error error;
    QCOMPARE(MigrationRunner::readVersion(database, &error), 0);

    const MigrationRunner runner(threeGoodMigrations());
    QVERIFY2(runner.migrate(database, &error), qPrintable(error.message()));
    QCOMPARE(MigrationRunner::readVersion(database, &error), 3);
    QVERIFY(tableExists(database, QStringLiteral("widget")));
    QVERIFY(tableExists(database, QStringLiteral("gadget")));
}

void TestProjectionMigrations::reopeningACurrentDatabaseChangesNothing()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("current.db"));
    const MigrationRunner runner(threeGoodMigrations());

    {
        Connection connection(path);
        QVERIFY(connection.isOpen());
        QSqlDatabase database = connection.database();
        Error error;
        QVERIFY2(runner.migrate(database, &error), qPrintable(error.message()));
        QSqlQuery insert(database);
        QVERIFY(insert.exec(QStringLiteral("INSERT INTO widget (id, name) VALUES (1, 'kept')")));
    }

    Connection reopened(path);
    QVERIFY(reopened.isOpen());
    QSqlDatabase database = reopened.database();
    Error error;

    // Re-running a fully applied set must be a no-op, not a re-execution: the
    // migrations are not idempotent and would fail if they ran twice.
    QVERIFY2(runner.migrate(database, &error), qPrintable(error.message()));
    QCOMPARE(MigrationRunner::readVersion(database, &error), 3);

    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM widget WHERE id = 1")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("kept"));
}

void TestProjectionMigrations::aPreviousVersionIsUpgradedWithoutLosingData()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("old.db"));
    const MigrationRunner runner(threeGoodMigrations());

    {
        Connection connection(path);
        QVERIFY(connection.isOpen());
        QSqlDatabase database = connection.database();
        Error error;
        QVERIFY2(runner.migrate(database, 1, &error), qPrintable(error.message()));
        QCOMPARE(MigrationRunner::readVersion(database, &error), 1);
        QVERIFY(!tableExists(database, QStringLiteral("gadget")));

        QSqlQuery insert(database);
        QVERIFY(insert.exec(QStringLiteral("INSERT INTO widget (id, name) VALUES (7, 'older')")));
    }

    Connection reopened(path);
    QVERIFY(reopened.isOpen());
    QSqlDatabase database = reopened.database();
    Error error;
    QVERIFY2(runner.migrate(database, &error), qPrintable(error.message()));
    QCOMPARE(MigrationRunner::readVersion(database, &error), 3);

    // The row written by the older schema is still there, and reads through
    // the column the upgrade added.
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("SELECT name, colour FROM widget WHERE id = 7")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("older"));
    QCOMPARE(query.value(1).toString(), QString());
}

void TestProjectionMigrations::aFailedMigrationRollsBackAndKeepsThePreviousVersion()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    Connection connection(temporary.filePath(QStringLiteral("failing.db")));
    QVERIFY(connection.isOpen());
    QSqlDatabase database = connection.database();

    QList<Migration> migrations = threeGoodMigrations();
    // The second statement fails after the first has already taken effect, so
    // the test proves the whole migration is undone rather than just the
    // failing statement.
    migrations[1].statements = {
        QStringLiteral("CREATE TABLE halfway (id INTEGER PRIMARY KEY)"),
        QStringLiteral("CREATE TABLE halfway (id INTEGER PRIMARY KEY)"),
    };
    const MigrationRunner runner(migrations);

    Error error;
    QVERIFY(!runner.migrate(database, &error));
    QVERIFY(error.isError());
    QVERIFY2(error.message().contains(QStringLiteral("add-colour")), qPrintable(error.message()));

    QCOMPARE(MigrationRunner::readVersion(database, &error), 1);
    QVERIFY(tableExists(database, QStringLiteral("widget")));
    QVERIFY(!tableExists(database, QStringLiteral("halfway")));
    QVERIFY(!tableExists(database, QStringLiteral("gadget")));

    // The database is still usable at the version it reports.
    QSqlQuery insert(database);
    QVERIFY(insert.exec(QStringLiteral("INSERT INTO widget (id, name) VALUES (1, 'usable')")));
}

void TestProjectionMigrations::aKilledMigrationLeavesTheDatabaseAtThePreviousVersion()
{
    QVERIFY2(QFileInfo::exists(m_helperPath), qPrintable(m_helperPath));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("killed.db"));

    // A rolled-back transaction and a process that never reached the rollback
    // are different failures. This one kills the process mid-migration, which
    // is what a power loss or a task manager actually does.
    QProcess helper;
    helper.start(m_helperPath, {QStringLiteral("crash-during-migration"), path});
    QVERIFY(helper.waitForStarted(30'000));
    QVERIFY(helper.waitForFinished(120'000));
    QVERIFY(helper.exitCode() != 0);

    Connection connection(path);
    QVERIFY(connection.isOpen());
    QSqlDatabase database = connection.database();
    Error error;

    QCOMPARE(MigrationRunner::readVersion(database, &error), 1);
    QVERIFY(tableExists(database, QStringLiteral("widget")));
    QVERIFY(!tableExists(database, QStringLiteral("gadget")));

    // Recovery is simply running the migrations again.
    const MigrationRunner runner(threeGoodMigrations());
    QVERIFY2(runner.migrate(database, &error), qPrintable(error.message()));
    QCOMPARE(MigrationRunner::readVersion(database, &error), 3);
    QVERIFY(tableExists(database, QStringLiteral("gadget")));
}

void TestProjectionMigrations::aNewerDatabaseIsRefusedRatherThanDowngraded()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("newer.db"));

    {
        Connection connection(path);
        QVERIFY(connection.isOpen());
        QSqlDatabase database = connection.database();
        Error error;
        const MigrationRunner future(threeGoodMigrations());
        QVERIFY2(future.migrate(database, &error), qPrintable(error.message()));
    }

    // An older build knows only the first two migrations.
    QList<Migration> known = threeGoodMigrations();
    known.removeLast();
    const MigrationRunner older(known);

    Connection connection(path);
    QVERIFY(connection.isOpen());
    QSqlDatabase database = connection.database();
    Error error;
    QVERIFY(!older.migrate(database, &error));
    PIMIO_COMPARE_ENUM(error.code(), ErrorCode::Conflict);

    // Refusing must not damage what it refused to touch.
    QCOMPARE(MigrationRunner::readVersion(database, &error), 3);
    QVERIFY(tableExists(database, QStringLiteral("gadget")));
}

void TestProjectionMigrations::nonContiguousVersionsAreRejected()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    Connection connection(temporary.filePath(QStringLiteral("gap.db")));
    QVERIFY(connection.isOpen());
    QSqlDatabase database = connection.database();

    const MigrationRunner runner({
        Migration{1, QStringLiteral("first"), {QStringLiteral("CREATE TABLE a (id INTEGER)")}},
        Migration{3, QStringLiteral("third"), {QStringLiteral("CREATE TABLE b (id INTEGER)")}},
    });

    Error error;
    QVERIFY(!runner.migrate(database, &error));
    QVERIFY(error.isError());
    QVERIFY(!tableExists(database, QStringLiteral("a")));

    // The shipped list must satisfy the rule it enforces.
    const MigrationRunner shipped(projectionMigrations());
    QVERIFY(shipped.latestVersion() >= 1);
    Error shippedError;
    QVERIFY2(shipped.migrate(database, &shippedError), qPrintable(shippedError.message()));
}

QTEST_MAIN(TestProjectionMigrations)

#include "tst_projection_migrations.moc"
