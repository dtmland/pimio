#include "pimio/projection/projection_database.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace pimio::projection {

using core::ContentFingerprint;
using core::DurableStore;
using core::Error;
using core::ErrorCode;
using core::MediaId;
using core::MediaRecord;

namespace {

constexpr auto kStateTokenKey = "durableStateToken";

void setError(Error *error, ErrorCode code, const QString &message)
{
    if (error != nullptr) {
        *error = Error(code, message);
    }
}

} // namespace

class ProjectionDatabase::Private
{
public:
    /// Drops the connection. The QSqlDatabase handle must be gone before
    /// removeDatabase() is called or Qt warns and keeps the connection alive,
    /// which would hold the damaged file open across a recovery.
    void discardConnection()
    {
        {
            QSqlDatabase database = QSqlDatabase::database(connectionName, false);
            if (database.isValid()) {
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        open = false;
        schemaVersion = 0;
    }

    // Each instance gets its own Qt connection name so two projections, or a
    // test and the code it tests, never share a handle.
    QString connectionName = QStringLiteral("pimio-projection-")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path;
    int schemaVersion = 0;
    bool open = false;

    QSqlDatabase database() const { return QSqlDatabase::database(connectionName, false); }

    bool prepared(QSqlQuery &query, const QString &statement, Error *error) const
    {
        if (!query.prepare(statement)) {
            setError(error, ErrorCode::Internal,
                     QStringLiteral("Could not prepare %1: %2")
                         .arg(statement, query.lastError().text()));
            return false;
        }
        return true;
    }

    bool applyPragmas(QSqlDatabase &db, bool onDisk, Error *error) const;
    bool checkIntegrity(QSqlDatabase &db, Error *error) const;
    bool insertRecord(QSqlDatabase &db, const MediaRecord &record, Error *error) const;
    QList<MediaId> idsFrom(const QString &statement, const QVariantList &bindings,
                           Error *error) const;
};

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
                          kind, file_name, folder_path,
                          capture_sort_key, capture_has_offset,
                          camera_make, camera_model, pixel_width, pixel_height,
                          duration_ms, rating, caption, latitude, longitude
                      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                  )"),
                  error)) {
        return false;
    }

    const auto &metadata = record.metadata;
    query.addBindValue(record.id.value());
    query.addBindValue(QString::fromUtf8(QJsonDocument(record.toJson()).toJson(
        QJsonDocument::Compact)));
    query.addBindValue(record.fingerprint.algorithm());
    query.addBindValue(record.fingerprint.digest());
    query.addBindValue(record.identity.absolutePath);
    query.addBindValue(record.identity.volumeId);
    query.addBindValue(record.identity.fileId);
    query.addBindValue(record.identity.sizeBytes);
    query.addBindValue(record.identity.lastModified.isValid()
                           ? QVariant(record.identity.lastModified.toMSecsSinceEpoch())
                           : QVariant(QMetaType(QMetaType::LongLong)));
    query.addBindValue(core::toString(metadata.kind));
    query.addBindValue(metadata.fileName);
    query.addBindValue(metadata.folderPath);
    query.addBindValue(metadata.captureTime.sortKeyMSecs());
    query.addBindValue(metadata.captureTime.hasKnownOffset() ? 1 : 0);
    query.addBindValue(metadata.cameraMake);
    query.addBindValue(metadata.cameraModel);
    query.addBindValue(metadata.pixelWidth);
    query.addBindValue(metadata.pixelHeight);
    query.addBindValue(metadata.durationMs);
    query.addBindValue(metadata.rating);
    query.addBindValue(metadata.caption);
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
        || !clear.exec(QStringLiteral("DELETE FROM media"))) {
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
