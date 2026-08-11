#include "pimio/projection/projection_database.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include <utility>

namespace pimio::projection {

using core::ContentFingerprint;
using core::DurableStore;
using core::Error;
using core::ErrorCode;
using core::MediaId;
using core::MediaKind;
using core::MediaRecord;

namespace {

constexpr auto kStateTokenKey = "durableStateToken";

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

qsizetype ProjectionDatabase::recordCount(Error *error) const{
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

QList<MediaId> ProjectionDatabase::idsByCaptureTime(int offset, int limit, Error *error) const{
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
