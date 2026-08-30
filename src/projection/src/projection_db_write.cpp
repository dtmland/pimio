#include "projection_db_private.h"

#include <QJsonDocument>

namespace pimio::projection {

using core::DurableStore;
using core::Error;
using core::ErrorCode;
using core::MediaId;
using core::MediaRecord;

bool ProjectionDatabase::Private::insertRecord(QSqlDatabase &db, const MediaRecord &record,
                                               Error *error) const
{
    QSqlQuery query(db);
    if (!prepared(query,
                  QStringLiteral(R"(
                      INSERT INTO media (
                          id, record_json,
                          fingerprint_algorithm, fingerprint_digest,
                          absolute_path, volume_id, file_id, size_bytes, last_modified_ms,
                          kind, file_name, file_extension, folder_path,
                          capture_sort_key, capture_has_offset,
                          camera_make, camera_model, pixel_width, pixel_height,
                          duration_ms, rating, caption, latitude, longitude
                      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                                ?, ?)
                  )"),
                  error)) {
        return false;
    }

    const auto &metadata = record.metadata;
    query.addBindValue(record.id.value());
    query.addBindValue(QString::fromUtf8(QJsonDocument(record.toJson()).toJson(
        QJsonDocument::Compact)));
    query.addBindValue(notNull(record.fingerprint.algorithm()));
    query.addBindValue(notNull(record.fingerprint.digest()));
    query.addBindValue(notNull(record.identity.absolutePath));
    query.addBindValue(notNull(record.identity.volumeId));
    query.addBindValue(notNull(record.identity.fileId));
    query.addBindValue(record.identity.sizeBytes);
    query.addBindValue(record.identity.lastModified.isValid()
                           ? QVariant(record.identity.lastModified.toMSecsSinceEpoch())
                           : QVariant(QMetaType(QMetaType::LongLong)));
    query.addBindValue(core::toString(metadata.kind));
    query.addBindValue(notNull(metadata.fileName));
    query.addBindValue(fileExtension(metadata.fileName));
    query.addBindValue(notNull(metadata.folderPath));
    query.addBindValue(metadata.captureTime.sortKeyMSecs());
    query.addBindValue(metadata.captureTime.hasKnownOffset() ? 1 : 0);
    query.addBindValue(notNull(metadata.cameraMake));
    query.addBindValue(notNull(metadata.cameraModel));
    query.addBindValue(metadata.pixelWidth);
    query.addBindValue(metadata.pixelHeight);
    query.addBindValue(metadata.durationMs);
    query.addBindValue(metadata.rating);
    query.addBindValue(notNull(metadata.caption));
    query.addBindValue(metadata.location.has_value()
                           ? QVariant(metadata.location->latitude())
                           : QVariant(QMetaType(QMetaType::Double)));
    query.addBindValue(metadata.location.has_value()
                           ? QVariant(metadata.location->longitude())
                           : QVariant(QMetaType(QMetaType::Double)));

    if (!query.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not project the record %1: %2")
                     .arg(record.id.value(), query.lastError().text()));
        return false;
    }

    for (const QString &tag : metadata.tags) {
        QSqlQuery tagQuery(db);
        if (!prepared(tagQuery,
                      QStringLiteral("INSERT OR IGNORE INTO media_tag (media_id, tag) VALUES "
                                     "(?, ?)"),
                      error)) {
            return false;
        }
        tagQuery.addBindValue(record.id.value());
        tagQuery.addBindValue(tag);
        if (!tagQuery.exec()) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Could not project the tag %1 of record %2: %3")
                         .arg(tag, record.id.value(), tagQuery.lastError().text()));
            return false;
        }
    }

    // Populate the full-text index.
    QSqlQuery ftsQuery(db);
    if (!prepared(ftsQuery,
                  QStringLiteral("INSERT INTO media_fts(id, caption, file_name) VALUES (?, ?, ?)"),
                  error)) {
        return false;
    }
    ftsQuery.addBindValue(record.id.value());
    ftsQuery.addBindValue(metadata.caption);
    ftsQuery.addBindValue(metadata.fileName);
    if (!ftsQuery.exec()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not index the text of record %1: %2")
                     .arg(record.id.value(), ftsQuery.lastError().text()));
        return false;
    }

    return true;
}

bool ProjectionDatabase::rebuildFrom(const DurableStore &store, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The projection is not open."));
        return false;
    }
    if (!store.isAvailable()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable, so the projection cannot be "
                                "rebuilt from it."));
        return false;
    }

    // The token is read before the records, not after. If the store changes
    // while the rebuild runs, the projection records the older token and is
    // seen as stale, which is the safe direction to be wrong in.
    const QString token = store.stateToken();
    if (token.isEmpty()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store did not report a state token."));
        return false;
    }

    // The caller's Error may already hold an unrelated failure, so the store
    // is questioned with a fresh one. Reading someone else's error as our own
    // result is how a healthy rebuild ends up reported as a failure.
    Error listError;
    const QList<MediaId> ids = store.listIds(&listError);
    if (listError.isError()) {
        if (error != nullptr) {
            *error = listError;
        }
        return false;
    }

    QSqlDatabase db = d->database();
    if (!db.transaction()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not start the rebuild transaction: %1")
                     .arg(db.lastError().text()));
        return false;
    }

    auto fail = [&db](Error *out, ErrorCode code, const QString &message) {
        db.rollback();
        setError(out, code, message);
        return false;
    };

    QSqlQuery clear(db);
    if (!clear.exec(QStringLiteral("DELETE FROM media_tag"))
        || !clear.exec(QStringLiteral("DELETE FROM media"))
        || !clear.exec(QStringLiteral("DELETE FROM media_fts"))) {
        return fail(error, ErrorCode::Internal,
                    QStringLiteral("Could not clear the projection: %1")
                        .arg(clear.lastError().text()));
    }

    for (const MediaId &id : ids) {
        Error loadError;
        const std::optional<MediaRecord> record = store.load(id, &loadError);
        if (!record.has_value()) {
            return fail(error, loadError.isError() ? loadError.code() : ErrorCode::NotFound,
                        QStringLiteral("Could not read the record %1 from the durable store: %2")
                            .arg(id.value(), loadError.message()));
        }
        Error insertError;
        if (!d->insertRecord(db, *record, &insertError)) {
            db.rollback();
            if (error != nullptr) {
                *error = insertError;
            }
            return false;
        }
    }

    QSqlQuery meta(db);
    if (!d->prepared(meta,
                     QStringLiteral("INSERT INTO projection_meta (key, value) VALUES (?, ?) "
                                    "ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
                     error)) {
        db.rollback();
        return false;
    }
    meta.addBindValue(QString::fromLatin1(kStateTokenKey));
    meta.addBindValue(token);
    if (!meta.exec()) {
        return fail(error, ErrorCode::Internal,
                    QStringLiteral("Could not record the durable state token: %1")
                        .arg(meta.lastError().text()));
    }

    if (!db.commit()) {
        const QString message = db.lastError().text();
        db.rollback();
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not commit the rebuild: %1").arg(message));
        return false;
    }
    return true;
}

bool ProjectionDatabase::applyRecords(const QList<MediaRecord> &records, Error *error)
{
    if (!d->open) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The projection is not open."));
        return false;
    }
    if (records.isEmpty()) {
        return true;
    }

    QSqlDatabase db = d->database();
    if (!db.transaction()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not start the projection transaction: %1")
                     .arg(db.lastError().text()));
        return false;
    }

    for (const MediaRecord &record : records) {
        // A record can arrive again with new content (a file edited between
        // two batches of the same scan), so its previous rows go first.
        for (const QString &statement : {QStringLiteral("DELETE FROM media_tag WHERE media_id = ?"),
                                         QStringLiteral("DELETE FROM media_fts WHERE id = ?"),
                                         QStringLiteral("DELETE FROM media WHERE id = ?")}) {
            QSqlQuery remove(db);
            if (!d->prepared(remove, statement, error)) {
                db.rollback();
                return false;
            }
            remove.addBindValue(record.id.value());
            if (!remove.exec()) {
                const QString message = remove.lastError().text();
                db.rollback();
                setError(error, ErrorCode::Internal,
                         QStringLiteral("Could not replace the projected record %1: %2")
                             .arg(record.id.value(), message));
                return false;
            }
        }

        Error insertError;
        if (!d->insertRecord(db, record, &insertError)) {
            db.rollback();
            if (error != nullptr) {
                *error = insertError;
            }
            return false;
        }
    }

    if (!db.commit()) {
        const QString message = db.lastError().text();
        db.rollback();
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not commit the projected records: %1").arg(message));
        return false;
    }
    return true;
}

} // namespace pimio::projection
