#include "pimio/projection/migration.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace pimio::projection {

using core::Error;
using core::ErrorCode;

namespace {

void setError(Error *error, ErrorCode code, const QString &message)
{
    if (error != nullptr) {
        *error = Error(code, message);
    }
}

bool execute(QSqlDatabase &database, const QString &statement, Error *error)
{
    QSqlQuery query(database);
    if (!query.exec(statement)) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("The statement %1 failed: %2")
                     .arg(statement, query.lastError().text()));
        return false;
    }
    return true;
}

} // namespace

MigrationRunner::MigrationRunner(QList<Migration> migrations)
    : m_migrations(std::move(migrations))
{
    std::sort(m_migrations.begin(), m_migrations.end(),
              [](const Migration &left, const Migration &right) {
                  return left.version < right.version;
              });
}

int MigrationRunner::latestVersion() const
{
    return m_migrations.isEmpty() ? 0 : m_migrations.last().version;
}

const QList<Migration> &MigrationRunner::migrations() const
{
    return m_migrations;
}

int MigrationRunner::readVersion(QSqlDatabase &database, Error *error)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("Could not read the schema version: %1")
                     .arg(query.lastError().text()));
        return -1;
    }
    return query.value(0).toInt();
}

bool MigrationRunner::migrate(QSqlDatabase &database, Error *error) const
{
    return migrate(database, -1, error);
}

bool MigrationRunner::migrate(QSqlDatabase &database, int targetVersion, Error *error) const
{
    // A gap or a repeat in the version sequence would make the stored version
    // ambiguous about which migrations have run, so it is rejected outright
    // rather than tolerated.
    for (int index = 0; index < m_migrations.size(); ++index) {
        if (m_migrations.at(index).version != index + 1) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Migration versions must be contiguous from 1; found %1 at "
                                    "position %2.")
                         .arg(m_migrations.at(index).version)
                         .arg(index + 1));
            return false;
        }
    }

    const int target = targetVersion < 0 ? latestVersion() : targetVersion;
    if (target > latestVersion()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Cannot migrate to version %1; the newest known version is %2.")
                     .arg(target)
                     .arg(latestVersion()));
        return false;
    }

    const int current = readVersion(database, error);
    if (current < 0) {
        return false;
    }
    if (current > target) {
        setError(error, ErrorCode::Conflict,
                 QStringLiteral("The database is at schema version %1, which is newer than the "
                                "requested version %2. It cannot be migrated backwards.")
                     .arg(current)
                     .arg(target));
        return false;
    }

    for (const Migration &migration : m_migrations) {
        if (migration.version <= current || migration.version > target) {
            continue;
        }

        // The version bump belongs to the same transaction as the statements
        // it describes. If the process dies here, SQLite rolls the whole thing
        // back and the database is still at the previous version.
        if (!database.transaction()) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Could not start a transaction for migration %1: %2")
                         .arg(migration.name, database.lastError().text()));
            return false;
        }

        bool ok = true;
        for (const QString &statement : migration.statements) {
            if (!execute(database, statement, error)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            ok = execute(database,
                         QStringLiteral("PRAGMA user_version = %1").arg(migration.version), error);
        }

        if (!ok) {
            database.rollback();
            if (error != nullptr) {
                *error = Error(error->code(),
                               QStringLiteral("Migration %1 failed and was rolled back: %2")
                                   .arg(migration.name, error->message()));
            }
            return false;
        }

        if (!database.commit()) {
            const QString message = database.lastError().text();
            database.rollback();
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Migration %1 could not be committed: %2")
                         .arg(migration.name, message));
            return false;
        }
    }

    return true;
}

} // namespace pimio::projection
