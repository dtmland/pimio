#include "pimio/projection/migration.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace pimio::core;
using namespace pimio::projection;

namespace {

/// Runs migration 1, then dies inside migration 2's transaction.
///
/// The kill happens between the statements and the commit, which is the window
/// a crash has to survive: SQLite must roll the transaction back on the next
/// open and leave the database at version 1.
int crashDuringMigration(const QString &path)
{
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                      QStringLiteral("helper"));
    database.setDatabaseName(path);
    if (!database.open()) {
        std::fprintf(stderr, "could not open %s\n", qPrintable(path));
        return 2;
    }

    QSqlQuery pragma(database);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    const MigrationRunner first({
        Migration{1, QStringLiteral("create-widget"),
                  {QStringLiteral("CREATE TABLE widget (id INTEGER PRIMARY KEY, name TEXT)")}},
    });
    Error error;
    if (!first.migrate(database, &error)) {
        std::fprintf(stderr, "migration 1 failed: %s\n", qPrintable(error.message()));
        return 3;
    }

    if (!database.transaction()) {
        std::fprintf(stderr, "could not start the transaction\n");
        return 4;
    }
    QSqlQuery query(database);
    query.exec(QStringLiteral("ALTER TABLE widget ADD COLUMN colour TEXT NOT NULL DEFAULT ''"));
    query.exec(QStringLiteral("CREATE TABLE gadget (id INTEGER PRIMARY KEY)"));
    query.exec(QStringLiteral("PRAGMA user_version = 2"));

    std::fflush(nullptr);
    // No unwinding, no destructors, no rollback: exactly what a killed process
    // leaves behind.
    std::_Exit(9);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() < 3) {
        std::fprintf(stderr, "usage: %s crash-during-migration <database-path>\n",
                     qPrintable(arguments.value(0)));
        return 1;
    }
    if (arguments.at(1) == QLatin1String("crash-during-migration")) {
        return crashDuringMigration(arguments.at(2));
    }
    std::fprintf(stderr, "unknown mode %s\n", qPrintable(arguments.at(1)));
    return 1;
}
