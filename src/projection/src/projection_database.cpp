#include "projection_db_private.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <utility>

namespace pimio::projection {

using core::ContentFingerprint;
using core::DurableStore;
using core::Error;
using core::ErrorCode;
using core::MediaId;
using core::MediaKind;
using core::MediaRecord;

void setError(Error *error, ErrorCode code, const QString &message)
{
    if (error != nullptr) {
        *error = Error(code, message);
    }
}

/// Every TEXT column projected here is declared NOT NULL. A default-
/// constructed QString (for example an unset MediaMetadata::cameraMake, or
/// FileIdentity::volumeId on a platform that could not determine one) is a
/// *null* QString rather than merely empty, and Qt's SQLite driver binds a
/// null QString as SQL NULL, not as an empty string. Coalescing here keeps
/// every optional-in-practice text field projectable without weakening the
/// schema's NOT NULL columns, which exist so every other query on them can
/// assume a real (if empty) string.
QString notNull(const QString &value)
{
    return value.isNull() ? QString(QLatin1String("")) : value;
}

/// Lower-cased extension of \a fileName without the dot, or an empty string
/// when there is none.
///
/// A leading dot is not a separator ("`.profile`" has no extension), and the
/// last dot wins ("`clip.tar.gz`" is a `gz`), which is what a user sorting by
/// file type expects to see.
QString fileExtension(const QString &fileName)
{
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == fileName.size() - 1) {
        return QString(QLatin1String(""));
    }
    return fileName.mid(dot + 1).toLower();
}

/// Turns what the user typed into an FTS5 MATCH expression.
///
/// The text is never handed to FTS5 as-is. FTS5 reads its own operators in a
/// bare string, so an ordinary search such as "foo(bar", "AND" or "gate:sun"
/// is a syntax error rather than a search, and the user sees a failure for
/// text they were entitled to type. Each whitespace-separated term is instead
/// wrapped in a quoted phrase, which has no operators inside it, so any
/// character is searchable.
///
/// Each phrase also carries a trailing \c * . The unicode61 tokenizer breaks
/// on character category, not on meaning, so an unbroken CJK run like
/// "東京タワー" is a single token: without the prefix operator, searching
/// "東京" would find nothing. The same operator gives ASCII the search-as-you-
/// type behaviour a search box is expected to have.
///
/// Returns an empty string when there is nothing to search for, which the
/// caller reports as an empty result rather than as an error.
QString ftsMatchExpression(const QString &query)
{
    const QStringList terms = query.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
    QStringList phrases;
    phrases.reserve(terms.size());
    for (const QString &term : terms) {
        // A literal double quote is escaped by doubling it, so the phrase
        // cannot be closed early by anything the user typed.
        QString escaped = term;
        escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
        phrases.append(QLatin1Char('"') + escaped + QLatin1String("\"*"));
    }
    return phrases.join(QLatin1Char(' '));
}

void ProjectionDatabase::Private::discardConnection()
{
    {
        QSqlDatabase connection = QSqlDatabase::database(connectionName, false);
        if (connection.isValid()) {
            connection.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    open = false;
    schemaVersion = 0;
}

QSqlDatabase ProjectionDatabase::Private::database() const
{
    return QSqlDatabase::database(connectionName, false);
}

bool ProjectionDatabase::Private::prepared(QSqlQuery &query, const QString &statement,
                                           Error *error) const
{
    if (!query.prepare(statement)) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not prepare %1: %2")
                     .arg(statement, query.lastError().text()));
        return false;
    }
    return true;
}

bool ProjectionDatabase::Private::applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const
{
    QSqlQuery query(db);

    // Write-ahead logging: a reader is never blocked by the rebuild, and a
    // crash mid-rebuild rolls back rather than truncating the file. It is
    // meaningless for an in-memory database, so it is not requested there.
    if (onDisk && (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !query.next()
                   || query.value(0).toString().compare(QLatin1String("wal"),
                                                        Qt::CaseInsensitive)
                       != 0)) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not enable write-ahead logging on %1: %2")
                     .arg(path, query.lastError().text()));
        return false;
    }

    // NORMAL is the documented safe pairing with WAL: a power loss can lose
    // the most recent transactions but cannot corrupt the database. Losing
    // recent transactions is acceptable here precisely because the durable
    // store, not this file, is the ground truth.
    if (!query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"))) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not set the synchronous mode: %1")
                     .arg(query.lastError().text()));
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not enable foreign keys: %1")
                     .arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ProjectionDatabase::Private::checkIntegrity(QSqlDatabase &db, Error *error) const
{
    // quick_check rather than integrity_check: it catches the damage that
    // matters at open time without reading every page of a large library.
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA quick_check")) || !query.next()) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The projection at %1 could not be checked: %2")
                     .arg(path, query.lastError().text()));
        return false;
    }
    const QString result = query.value(0).toString();
    if (result.compare(QLatin1String("ok"), Qt::CaseInsensitive) != 0) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The projection at %1 is damaged: %2").arg(path, result));
        return false;
    }
    return true;
}

ProjectionDatabase::ProjectionDatabase()
    : d(std::make_unique<Private>())
{
}

ProjectionDatabase::~ProjectionDatabase()
{
    close();
}

bool ProjectionDatabase::open(const QString &path, Error *error)
{
    close();
    d->path = path;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The Qt SQLite driver is not available."));
        return false;
    }

    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), d->connectionName);
        db.setDatabaseName(path);
        if (!db.open()) {
            setError(error, ErrorCode::PermissionDenied,
                     QStringLiteral("Could not open the projection at %1: %2")
                         .arg(path, db.lastError().text()));
        } else {
            // A file that exists but is not a database only reveals itself
            // when it is read, so it is read before anything is written to it.
            const bool preexisting = QFileInfo(path).size() > 0;
            ok = (!preexisting || d->checkIntegrity(db, error))
                 && d->applyPragmas(db, true, error)
                 && MigrationRunner(projectionMigrations()).migrate(db, error);
            if (ok) {
                d->schemaVersion = MigrationRunner::readVersion(db, error);
                ok = d->schemaVersion >= 0;
            }
        }
    }

    if (!ok) {
        d->discardConnection();
        return false;
    }
    d->open = true;
    return true;
}

bool ProjectionDatabase::remove(const QString &path, Error *error)
{
    // The write-ahead log and shared-memory files are part of the database.
    // Deleting only the main file leaves a recovery reading half of a database
    // it thinks it discarded.
    bool removed = false;
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"),
                                  QStringLiteral("-journal")}) {
        const QString candidate = path + suffix;
        if (!QFile::exists(candidate)) {
            continue;
        }
        if (!QFile::remove(candidate)) {
            setError(error, ErrorCode::PermissionDenied,
                     QStringLiteral("Could not delete %1.").arg(candidate));
            return false;
        }
        removed = true;
    }
    if (!removed) {
        setError(error, ErrorCode::NotFound,
                 QStringLiteral("There is no projection at %1 to delete.").arg(path));
        return false;
    }
    return true;
}

bool ProjectionDatabase::openInMemory(Error *error)
{
    close();
    d->path = QStringLiteral(":memory:");

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The Qt SQLite driver is not available."));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), d->connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    bool ok = false;
    if (!db.open()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("Could not open an in-memory projection: %1")
                     .arg(db.lastError().text()));
    } else {
        ok = d->applyPragmas(db, false, error)
             && MigrationRunner(projectionMigrations()).migrate(db, error);
        if (ok) {
            d->schemaVersion = MigrationRunner::readVersion(db, error);
            ok = d->schemaVersion >= 0;
        }
    }
    if (!ok) {
        db = QSqlDatabase();
        d->discardConnection();
        return false;
    }
    d->open = true;
    return true;
}

void ProjectionDatabase::close()
{
    if (!d->open) {
        return;
    }
    d->discardConnection();
}

bool ProjectionDatabase::isOpen() const
{
    return d->open;
}

const QString &ProjectionDatabase::path() const
{
    return d->path;
}

int ProjectionDatabase::schemaVersion() const
{
    return d->schemaVersion;
}

QString ProjectionDatabase::projectedStateToken(Error *error) const
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The projection is not open."));
        return {};
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(query, QStringLiteral("SELECT value FROM projection_meta WHERE key = ?"),
                     error)) {
        return {};
    }
    query.addBindValue(QString::fromLatin1(kStateTokenKey));
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not read the projected state token: %1")
                     .arg(query.lastError().text()));
        return {};
    }
    if (!query.next()) {
        return {};
    }
    return query.value(0).toString();
}

bool ProjectionDatabase::isStale(const DurableStore &store, Error *error) const
{
    Error tokenError;
    const QString projected = projectedStateToken(&tokenError);
    if (tokenError.isError()) {
        if (error != nullptr) {
            *error = tokenError;
        }
        return true;
    }
    const QString current = store.stateToken();
    // An empty current token means the store could not answer. Treating that
    // as fresh would be the dangerous direction, so it counts as stale.
    return current.isEmpty() || projected.isEmpty() || projected != current;
}

} // namespace pimio::projection
