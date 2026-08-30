#include "job_queue_private.h"

namespace pimio::projection {

// Shared helpers visible to all job_queue translation units.
void jqSetError(Error *error, ErrorCode code, const QString &message)
{
    if (error != nullptr) {
        *error = Error(code, message);
    }
}

bool jqExecuteStatement(QSqlDatabase &db, const QString &statement, Error *error)
{
    QSqlQuery query(db);
    if (!query.exec(statement)) {
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Statement failed (%1): %2")
                       .arg(statement, query.lastError().text()));
        return false;
    }
    return true;
}

// Column order must match kJobSelectCols exactly.
const QLatin1StringView kJobSelectCols{
    "id, kind, priority, state, coalescing_key, payload, "
    "attempts, max_attempts, created_at_ms, not_before_ms, last_error"};

JobRecord recordFromQuery(QSqlQuery &query)
{
    JobRecord record;
    record.id = JobId(query.value(0).toString());
    record.kind = core::jobKindFromString(query.value(1).toString());
    record.priority = static_cast<core::JobPriority>(query.value(2).toInt());
    record.state = core::jobStateFromString(query.value(3).toString());
    record.coalescingKey = query.value(4).toString();
    record.payload =
        QJsonDocument::fromJson(query.value(5).toString().toUtf8()).object();
    record.attempts = query.value(6).toInt();
    record.maxAttempts = query.value(7).toInt();
    if (!query.value(8).isNull()) {
        record.createdAt =
            QDateTime::fromMSecsSinceEpoch(query.value(8).toLongLong(), Qt::UTC);
    }
    if (!query.value(9).isNull()) {
        record.notBefore =
            QDateTime::fromMSecsSinceEpoch(query.value(9).toLongLong(), Qt::UTC);
    }
    record.lastError =
        Error::fromJson(QJsonDocument::fromJson(query.value(10).toString().toUtf8()).object());
    return record;
}

void JobQueue::Private::discardConnection()
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isValid()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    open = false;
}

QSqlDatabase JobQueue::Private::database() const
{
    return QSqlDatabase::database(connectionName, false);
}

bool JobQueue::Private::prepared(QSqlQuery &query, const QString &statement,
                                 Error *error) const
{
    if (!query.prepare(statement)) {
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not prepare (%1): %2")
                       .arg(statement, query.lastError().text()));
        return false;
    }
    return true;
}

bool JobQueue::Private::applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const
{
    QSqlQuery query(db);

    if (onDisk && (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !query.next()
                   || query.value(0).toString().compare(QLatin1String("wal"),
                                                        Qt::CaseInsensitive)
                       != 0)) {
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not enable WAL on %1: %2")
                       .arg(path, query.lastError().text()));
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"))) {
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not set synchronous mode: %1")
                       .arg(query.lastError().text()));
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not enable foreign keys: %1")
                       .arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool JobQueue::Private::setup(QSqlDatabase &db, bool onDisk, Error *error)
{
    return applyPragmas(db, onDisk, error)
           && MigrationRunner(jobQueueMigrations()).migrate(db, error);
}

JobQueue::JobQueue()
    : d(std::make_unique<Private>())
{
}

JobQueue::~JobQueue()
{
    close();
}

bool JobQueue::open(const QString &path, Error *error)
{
    close();
    d->path = path;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("The Qt SQLite driver is not available."));
        return false;
    }

    bool ok = false;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), d->connectionName);
        db.setDatabaseName(path);
        if (!db.open()) {
            jqSetError(error, ErrorCode::PermissionDenied,
                       QStringLiteral("Could not open the job queue at %1: %2")
                           .arg(path, db.lastError().text()));
        } else {
            ok = d->setup(db, true, error);
        }
    }
    if (!ok) {
        d->discardConnection();
        return false;
    }
    d->open = true;
    return true;
}

bool JobQueue::openInMemory(Error *error)
{
    close();
    d->path = QStringLiteral(":memory:");

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("The Qt SQLite driver is not available."));
        return false;
    }

    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), d->connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    bool ok = false;
    if (!db.open()) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("Could not open an in-memory job queue: %1")
                       .arg(db.lastError().text()));
    } else {
        ok = d->setup(db, false, error);
    }
    if (!ok) {
        db = QSqlDatabase();
        d->discardConnection();
        return false;
    }
    d->open = true;
    return true;
}

void JobQueue::close()
{
    if (!d->open) {
        return;
    }
    d->discardConnection();
}

bool JobQueue::isOpen() const
{
    return d->open;
}

const QString &JobQueue::path() const
{
    return d->path;
}

} // namespace pimio::projection
