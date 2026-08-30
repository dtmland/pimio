#include "projection_db_private.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace pimio::projection {

using core::ContentFingerprint;
using core::Error;
using core::ErrorCode;
using core::MediaId;
using core::MediaKind;
using core::MediaRecord;

qsizetype ProjectionDatabase::recordCount(Error *error) const
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The projection is not open."));
        return -1;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM media")) || !query.next()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not count the projected records: %1")
                     .arg(query.lastError().text()));
        return -1;
    }
    return static_cast<qsizetype>(query.value(0).toLongLong());
}

QList<MediaId> ProjectionDatabase::Private::idsFrom(const QString &statement,
                                                    const QVariantList &bindings,
                                                    Error *error) const
{
    if (!open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The projection is not open."));
        return {};
    }
    QSqlDatabase db = database();
    QSqlQuery query(db);
    if (!prepared(query, statement, error)) {
        return {};
    }
    for (const QVariant &binding : bindings) {
        query.addBindValue(binding);
    }
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("The query %1 failed: %2").arg(statement,
                                                               query.lastError().text()));
        return {};
    }
    QList<MediaId> ids;
    while (query.next()) {
        ids.append(MediaId(query.value(0).toString()));
    }
    return ids;
}

QList<MediaId> ProjectionDatabase::listIds(Error *error) const
{
    return d->idsFrom(QStringLiteral("SELECT id FROM media ORDER BY id"), {}, error);
}

QList<MediaId> ProjectionDatabase::idsWithFingerprint(const ContentFingerprint &fingerprint,
                                                      Error *error) const
{
    return d->idsFrom(QStringLiteral("SELECT id FROM media WHERE fingerprint_algorithm = ? AND "
                                     "fingerprint_digest = ? ORDER BY id"),
                      {fingerprint.algorithm(), fingerprint.digest()}, error);
}

QList<MediaId> ProjectionDatabase::idsWithTag(const QString &tag, Error *error) const
{
    return d->idsFrom(
        QStringLiteral("SELECT media_id FROM media_tag WHERE tag = ? ORDER BY media_id"), {tag},
        error);
}

QList<MediaId> ProjectionDatabase::idsByCaptureTime(Error *error) const
{
    return d->idsFrom(
        QStringLiteral("SELECT id FROM media ORDER BY capture_sort_key, id"), {}, error);
}

QList<MediaId> ProjectionDatabase::idsByCaptureTime(int offset, int limit, Error *error) const
{
    const int sqlLimit = limit < 0 ? -1 : limit;
    return d->idsFrom(
        QStringLiteral("SELECT id FROM media ORDER BY capture_sort_key, id LIMIT ? OFFSET ?"),
        {sqlLimit, offset}, error);
}

QList<MediaId> ProjectionDatabase::idsSorted(SortKey key, Qt::SortOrder order,
                                             Error *error) const
{
    // The sort columns follow the requested direction; the id tie-break always
    // ascends, so reversing the sort does not also reshuffle rows that compare
    // equal.
    const QString direction = order == Qt::DescendingOrder ? QStringLiteral(" DESC")
                                                           : QString();
    QStringList columns;
    switch (key) {
    case SortKey::CaptureTime:
        columns << QStringLiteral("capture_sort_key");
        break;
    case SortKey::FileName:
        columns << QStringLiteral("file_name COLLATE NOCASE");
        break;
    case SortKey::FileDate:
        // last_modified_ms is nullable: a record whose file date could not be
        // read sorts with the oldest rather than disappearing from the view.
        columns << QStringLiteral("COALESCE(last_modified_ms, 0)");
        break;
    case SortKey::FileType:
        // Extension first, then name, so one file type reads as a list rather
        // than as an arbitrary interleaving.
        columns << QStringLiteral("file_extension")
                << QStringLiteral("file_name COLLATE NOCASE");
        break;
    case SortKey::FileSize:
        columns << QStringLiteral("size_bytes");
        break;
    }

    QStringList orderBy;
    for (const QString &column : std::as_const(columns)) {
        orderBy << column + direction;
    }
    orderBy << QStringLiteral("id");

    return d->idsFrom(QStringLiteral("SELECT id FROM media ORDER BY %1")
                              .arg(orderBy.join(QStringLiteral(", "))),
                      {}, error);
}

QList<MediaId> ProjectionDatabase::idsWithKind(MediaKind kind, Error *error) const
{
    return d->idsFrom(
        QStringLiteral("SELECT id FROM media WHERE kind = ? ORDER BY capture_sort_key, id"),
        {toString(kind)}, error);
}

QList<MediaId> ProjectionDatabase::idsWithMinimumRating(int minRating, Error *error) const
{
    return d->idsFrom(
        QStringLiteral(
            "SELECT id FROM media WHERE rating >= ? ORDER BY capture_sort_key, id"),
        {minRating}, error);
}

QList<MediaId> ProjectionDatabase::searchText(const QString &query, Error *error) const
{
    const QString expression = ftsMatchExpression(query);
    if (expression.isEmpty()) {
        return {};
    }
    return d->idsFrom(QStringLiteral("SELECT id FROM media_fts WHERE media_fts MATCH ? "
                                     "ORDER BY rank"),
                      {expression}, error);
}

std::optional<MediaRecord> ProjectionDatabase::load(const MediaId &id, Error *error) const
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The projection is not open."));
        return std::nullopt;
    }
    QSqlDatabase db = d->database();
    QSqlQuery query(db);
    if (!d->prepared(query, QStringLiteral("SELECT record_json FROM media WHERE id = ?"), error)) {
        return std::nullopt;
    }
    query.addBindValue(id.value());
    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not read the record %1: %2")
                     .arg(id.value(), query.lastError().text()));
        return std::nullopt;
    }
    if (!query.next()) {
        setError(error, ErrorCode::NotFound,
                 QStringLiteral("No projected record for %1.").arg(id.value()));
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(query.value(0).toString().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The projected record %1 is not valid JSON: %2")
                     .arg(id.value(), parseError.errorString()));
        return std::nullopt;
    }
    return MediaRecord::fromJson(document.object());
}

} // namespace pimio::projection
