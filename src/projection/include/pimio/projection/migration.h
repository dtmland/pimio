#pragma once

#include "pimio/core/error.h"

#include <QList>
#include <QString>
#include <QStringList>

class QSqlDatabase;

namespace pimio::projection {

/// One forward step of the projection schema.
///
/// There is no downgrade path on purpose. The database is a disposable cache
/// rebuilt from the durable store, so an older build meeting a newer cache
/// deletes it rather than trying to undo work it does not understand.
struct Migration
{
    /// Schema version this migration produces. Versions start at 1 and are
    /// contiguous, so the version alone says which migrations have run.
    int version = 0;

    /// Short identifier used in error messages. Not stored in the database.
    QString name;

    /// Statements executed in order, inside the migration's transaction.
    QStringList statements;
};

/// Applies schema migrations to a SQLite database.
///
/// The runner is deliberately independent of pimio's schema: it takes the
/// migration list as data so the mechanism can be tested with migrations that
/// exist only to test it, rather than by inventing product schema changes.
class MigrationRunner
{
public:
    explicit MigrationRunner(QList<Migration> migrations);

    /// The version a fully migrated database reports. Zero when there are no
    /// migrations.
    int latestVersion() const;

    const QList<Migration> &migrations() const;

    /// Reads the schema version from `PRAGMA user_version`. A database that
    /// has never been migrated reports 0.
    static int readVersion(QSqlDatabase &database, core::Error *error);

    /// Migrates the database up to \a targetVersion, or to the latest version
    /// when \a targetVersion is negative.
    ///
    /// Each migration runs in its own transaction together with the version
    /// bump, so an interrupted or failing migration leaves the database at the
    /// last version that completed. A database already newer than the target
    /// is an error: it is not this build's to modify.
    bool migrate(QSqlDatabase &database, int targetVersion, core::Error *error) const;

    bool migrate(QSqlDatabase &database, core::Error *error) const;

private:
    QList<Migration> m_migrations;
};

/// The projection schema. Append-only: an existing entry is never edited,
/// because databases in the field were built with it.
const QList<Migration> &projectionMigrations();

} // namespace pimio::projection
