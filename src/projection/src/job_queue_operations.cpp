#include "job_queue_private.h"

namespace pimio::projection {

bool JobQueue::recoverInterruptedJobs(Error *error)
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("The job queue is not open."));
        return false;
    }
    QSqlDatabase db = d->database();
    return jqExecuteStatement(
        db, QStringLiteral("UPDATE job SET state = 'pending' WHERE state = 'running'"), error);
}

std::optional<JobId> JobQueue::enqueue(const JobRecord &record, Error *error)
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
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
            jqSetError(error, ErrorCode::Internal,
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
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not enqueue job %1: %2")
                       .arg(record.id.value(), insert.lastError().text()));
        return std::nullopt;
    }
    return record.id;
}

QList<JobRecord> JobQueue::claim(int maxJobs, Error *error)
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
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
                .arg(kJobSelectCols),
            error)) {
        return {};
    }
    select.addBindValue(nowMs);
    select.addBindValue(maxJobs);

    if (!select.exec()) {
        jqSetError(error, ErrorCode::Internal,
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
            jqSetError(error, ErrorCode::Internal,
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
        jqSetError(error, ErrorCode::StorageUnavailable,
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
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not mark job %1 as succeeded: %2")
                       .arg(id.value(), query.lastError().text()));
        return false;
    }
    return true;
}

bool JobQueue::markFailed(const JobId &id, const core::Error &failure, Error *error)
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("The job queue is not open."));
        return false;
    }
    QSqlDatabase db = d->database();

    const QString failureJson = QString::fromUtf8(
        QJsonDocument(failure.toJson()).toJson(QJsonDocument::Compact));
    // In the CASE expression below, 'attempts' is the old (pre-update) value,
    // so attempts + 1 tests whether retries remain after counting this failure.
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
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not mark job %1 as failed: %2")
                       .arg(id.value(), query.lastError().text()));
        return false;
    }
    return true;
}

bool JobQueue::markCancelled(const JobId &id, Error *error)
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
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
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not mark job %1 as cancelled: %2")
                       .arg(id.value(), query.lastError().text()));
        return false;
    }
    return true;
}

std::optional<JobRecord> JobQueue::load(const JobId &id, Error *error) const
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("The job queue is not open."));
        return std::nullopt;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(query,
                     QStringLiteral("SELECT %1 FROM job WHERE id = ?").arg(kJobSelectCols),
                     error)) {
        return std::nullopt;
    }
    query.addBindValue(id.value());
    if (!query.exec()) {
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not load job %1: %2")
                       .arg(id.value(), query.lastError().text()));
        return std::nullopt;
    }
    if (!query.next()) {
        jqSetError(error, ErrorCode::NotFound,
                   QStringLiteral("No job with id %1.").arg(id.value()));
        return std::nullopt;
    }
    return recordFromQuery(query);
}

QList<JobRecord> JobQueue::listPending(Error *error) const
{
    if (!d->open) {
        jqSetError(error, ErrorCode::StorageUnavailable,
                   QStringLiteral("The job queue is not open."));
        return {};
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(
            query,
            QStringLiteral("SELECT %1 FROM job WHERE state = 'pending' "
                            "ORDER BY priority ASC, created_at_ms ASC, id ASC")
                .arg(kJobSelectCols),
            error)) {
        return {};
    }
    if (!query.exec()) {
        jqSetError(error, ErrorCode::Internal,
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
        jqSetError(error, ErrorCode::StorageUnavailable,
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
        jqSetError(error, ErrorCode::Internal,
                   QStringLiteral("Could not count pending jobs: %1")
                       .arg(query.lastError().text()));
        return -1;
    }
    return static_cast<qsizetype>(query.value(0).toLongLong());
}

} // namespace pimio::projection
