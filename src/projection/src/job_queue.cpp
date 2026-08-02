#include "pimio/projection/job_queue.h"

#include "pimio/projection/migration.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace pimio::projection {

using core::Error;
using core::ErrorCode;
using core::JobId;
using core::JobRecord;

namespace {

void setError(Error *error, ErrorCode code, const QString &message)
{
    if (error != nullptr) {
        *error = Error(code, message);
    }
}

bool executeStatement(QSqlDatabase &db, const QString &statement, Error *error)
{
    QSqlQuery query(db);
    if (!query.exec(statement)) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Statement failed (%1): %2")
                     .arg(statement, query.lastError().text()));
        return false;
    }
    return true;
}

// Column order must match kSelectCols exactly.
constexpr QLatin1StringView kSelectCols{
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

} // namespace

class JobQueue::Private
{
public:
    void discardConnection()
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

    QString connectionName = QStringLiteral("pimio-jobqueue-")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path;
    bool open = false;

    QSqlDatabase database() const { return QSqlDatabase::database(connectionName, false); }

    bool prepared(QSqlQuery &query, const QString &statement, Error *error) const
    {
        if (!query.prepare(statement)) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Could not prepare (%1): %2")
                         .arg(statement, query.lastError().text()));
            return false;
        }
        return true;
    }

    bool applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const;
    bool setup(QSqlDatabase &db, bool onDisk, Error *error);
};

bool JobQueue::Private::applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const
{
    QSqlQuery query(db);

    if (onDisk && (!query.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !query.next()
                   || query.value(0).toString().compare(QLatin1String("wal"),
                                                        Qt::CaseInsensitive)
                       != 0)) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not enable WAL on %1: %2")
                     .arg(path, query.lastError().text()));
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"))) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not set synchronous mode: %1")
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
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The Qt SQLite driver is not available."));
        return false;
    }

    bool ok = false;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), d->connectionName);
        db.setDatabaseName(path);
        if (!db.open()) {
            setError(error, ErrorCode::PermissionDenied,
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
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The Qt SQLite driver is not available."));
        return false;
    }

    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), d->connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    bool ok = false;
    if (!db.open()) {
        setError(error, ErrorCode::StorageUnavailable,
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

bool JobQueue::recoverInterruptedJobs(Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return false;
    }
    QSqlDatabase db = d->database();
    return executeStatement(
        db, QStringLiteral("UPDATE job SET state = 'pending' WHERE state = 'running'"), error);
}

std::optional<JobId> JobQueue::enqueue(const JobRecord &record, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return std::nullopt;
    }
    QSqlDatabase db = d->database();

    // Coalesce with pending work only. A running job may already have passed
    // the state affected by a newly observed event, so that event must leave
    // one follow-up job pending rather than being folded into work in flight.
    if (!record.coalescingKey.isEmpty()) {
        QSqlQuery check(db);
        if (!d->prepared(
                check,
                QStringLiteral("SELECT id FROM job "
                                "WHERE coalescing_key = ? "
                                "AND state = 'pending' "
                                "LIMIT 1"),
                error)) {
            return std::nullopt;
        }
        check.addBindValue(record.coalescingKey);
        if (!check.exec()) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Could not check for an existing coalesced job: %1")
                         .arg(check.lastError().text()));
            return std::nullopt;
        }
        if (check.next()) {
            return JobId(check.value(0).toString());
        }
    }

    QSqlQuery insert(db);
    if (!d->prepared(
            insert,
            QStringLiteral("INSERT INTO job "
                            "(id, kind, priority, state, coalescing_key, payload, "
                            " attempts, max_attempts, created_at_ms, not_before_ms, last_error) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
            error)) {
        return std::nullopt;
    }

    const QString payloadJson = QString::fromUtf8(
        QJsonDocument(record.payload).toJson(QJsonDocument::Compact));
    const QString lastErrorJson = QString::fromUtf8(
        QJsonDocument(record.lastError.toJson()).toJson(QJsonDocument::Compact));

    insert.addBindValue(record.id.value());
    insert.addBindValue(core::toString(record.kind));
    insert.addBindValue(static_cast<int>(record.priority));
    insert.addBindValue(core::toString(record.state));
    insert.addBindValue(record.coalescingKey.isNull() ? QStringLiteral("") : record.coalescingKey);
    insert.addBindValue(payloadJson);
    insert.addBindValue(record.attempts);
    insert.addBindValue(record.maxAttempts);
    insert.addBindValue(record.createdAt.isValid()
                            ? QVariant(record.createdAt.toMSecsSinceEpoch())
                            : QVariant(QMetaType(QMetaType::LongLong)));
    insert.addBindValue(record.notBefore.isValid()
                            ? QVariant(record.notBefore.toMSecsSinceEpoch())
                            : QVariant(QMetaType(QMetaType::LongLong)));
    insert.addBindValue(lastErrorJson);

    if (!insert.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not enqueue job %1: %2")
                     .arg(record.id.value(), insert.lastError().text()));
        return std::nullopt;
    }
    return record.id;
}

QList<JobRecord> JobQueue::claim(int maxJobs, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return {};
    }
    if (maxJobs <= 0) {
        return {};
    }

    QSqlDatabase db = d->database();
    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

    QSqlQuery select(db);
    if (!d->prepared(
            select,
            QStringLiteral("SELECT %1 FROM job "
                            "WHERE state = 'pending' "
                            "AND (not_before_ms IS NULL OR not_before_ms <= ?) "
                            "ORDER BY priority ASC, created_at_ms ASC, id ASC "
                            "LIMIT ?")
                .arg(kSelectCols),
            error)) {
        return {};
    }
    select.addBindValue(nowMs);
    select.addBindValue(maxJobs);

    if (!select.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not query pending jobs: %1")
                     .arg(select.lastError().text()));
        return {};
    }

    QList<JobRecord> records;
    while (select.next()) {
        records.append(recordFromQuery(select));
    }

    for (const JobRecord &rec : records) {
        QSqlQuery update(db);
        if (!d->prepared(
                update,
                QStringLiteral(
                    "UPDATE job SET state = 'running' WHERE id = ? AND state = 'pending'"),
                error)) {
            return {};
        }
        update.addBindValue(rec.id.value());
        if (!update.exec()) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Could not mark job %1 as running: %2")
                         .arg(rec.id.value(), update.lastError().text()));
            return {};
        }
    }

    for (JobRecord &rec : records) {
        rec.state = core::JobState::Running;
    }
    return records;
}

bool JobQueue::markSucceeded(const JobId &id, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return false;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(
            query,
            QStringLiteral(
                "UPDATE job SET state = 'succeeded', attempts = attempts + 1 "
                "WHERE id = ? AND state = 'running'"),
            error)) {
        return false;
    }
    query.addBindValue(id.value());
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not mark job %1 as succeeded: %2")
                     .arg(id.value(), query.lastError().text()));
        return false;
    }
    return true;
}

bool JobQueue::markFailed(const JobId &id, const core::Error &failure, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return false;
    }
    QSqlDatabase db = d->database();

    const QString failureJson = QString::fromUtf8(
        QJsonDocument(failure.toJson()).toJson(QJsonDocument::Compact));
    // In the CASE expression below, 'attempts' is the OLD (pre-update) value.
    // So (attempts + 1 < max_attempts) is true while retries remain after this
    // failure is counted.
    const int isRetryable = failure.isRetryable() ? 1 : 0;

    QSqlQuery query(db);
    if (!d->prepared(
            query,
            QStringLiteral(
                "UPDATE job "
                "SET attempts    = attempts + 1, "
                "    last_error  = ?, "
                "    state       = CASE WHEN (? = 1 AND attempts + 1 < max_attempts) "
                "                       THEN 'pending' ELSE 'failed' END, "
                "    not_before_ms = CASE WHEN (? = 1 AND attempts + 1 < max_attempts) "
                "                         THEN NULL ELSE not_before_ms END "
                "WHERE id = ? AND state = 'running'"),
            error)) {
        return false;
    }
    query.addBindValue(failureJson);
    query.addBindValue(isRetryable);
    query.addBindValue(isRetryable);
    query.addBindValue(id.value());

    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not mark job %1 as failed: %2")
                     .arg(id.value(), query.lastError().text()));
        return false;
    }
    return true;
}

bool JobQueue::markCancelled(const JobId &id, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return false;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(
            query,
            QStringLiteral("UPDATE job SET state = 'cancelled' "
                            "WHERE id = ? AND state IN ('pending', 'running')"),
            error)) {
        return false;
    }
    query.addBindValue(id.value());
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not mark job %1 as cancelled: %2")
                     .arg(id.value(), query.lastError().text()));
        return false;
    }
    return true;
}

std::optional<JobRecord> JobQueue::load(const JobId &id, Error *error) const
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return std::nullopt;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(query,
                     QStringLiteral("SELECT %1 FROM job WHERE id = ?").arg(kSelectCols),
                     error)) {
        return std::nullopt;
    }
    query.addBindValue(id.value());
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not load job %1: %2")
                     .arg(id.value(), query.lastError().text()));
        return std::nullopt;
    }
    if (!query.next()) {
        setError(error, ErrorCode::NotFound,
                 QStringLiteral("No job with id %1.").arg(id.value()));
        return std::nullopt;
    }
    return recordFromQuery(query);
}

QList<JobRecord> JobQueue::listPending(Error *error) const
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return {};
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(
            query,
            QStringLiteral("SELECT %1 FROM job WHERE state = 'pending' "
                            "ORDER BY priority ASC, created_at_ms ASC, id ASC")
                .arg(kSelectCols),
            error)) {
        return {};
    }
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not list pending jobs: %1")
                     .arg(query.lastError().text()));
        return {};
    }
    QList<JobRecord> records;
    while (query.next()) {
        records.append(recordFromQuery(query));
    }
    return records;
}

qsizetype JobQueue::pendingCount(Error *error) const
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The job queue is not open."));
        return -1;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(query,
                     QStringLiteral("SELECT COUNT(*) FROM job WHERE state = 'pending'"),
                     error)) {
        return -1;
    }
    if (!query.exec() || !query.next()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not count pending jobs: %1")
                     .arg(query.lastError().text()));
        return -1;
    }
    return static_cast<qsizetype>(query.value(0).toLongLong());
}

} // namespace pimio::projection
