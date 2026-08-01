#include "pimio/lore/lore_durable_store.h"

#include "lore_api.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <cstring>
#include <memory>

namespace pimio::lore {

namespace {

using core::Error;
using core::ErrorCode;

constexpr int kShardNameLength = 2;
constexpr char kCommitMarkerContents[] = "in-progress";

void setError(Error *target, ErrorCode code, const QString &message, QJsonObject context = {})
{
    if (target == nullptr) {
        return;
    }
    Error error(code, message);
    if (!context.isEmpty()) {
        error.setContext(std::move(context));
    }
    *target = error;
}

/// Collects the events of one synchronous LORE call.
///
/// The callback runs on a LORE worker thread, so every field is guarded. The
/// synchronous entry points return only after the terminating event, which is
/// why the collector can be read without further synchronization afterwards.
class Operation
{
public:
    lore_event_callback_config_t config()
    {
        lore_event_callback_config_t callback;
        callback.user_context = reinterpret_cast<uint64_t>(this);
        callback.func = &Operation::dispatch;
        return callback;
    }

    int32_t status = 0;
    QString message;

    bool sawStatusRevision = false;
    QString currentRevision;
    QString stagedRevision;
    quint64 reportedFileCount = 0;

    QString committedRevision;
    QList<core::Checkpoint> checkpoints;

private:
    static void dispatch(const lore_event_t *event, uint64_t context)
    {
        reinterpret_cast<Operation *>(context)->handle(event);
    }

    void handle(const lore_event_t *event)
    {
        const QMutexLocker locker(&m_mutex);
        switch (event->tag) {
        case LORE_EVENT_COMPLETE:
            status = event->complete.status;
            message = fromLoreString(event->complete.error.message);
            break;
        case LORE_EVENT_REPOSITORY_STATUS_REVISION:
            sawStatusRevision = true;
            currentRevision = hashToHex(event->repository_status_revision.revision);
            stagedRevision = isZeroHash(event->repository_status_revision.revision_staged)
                                 ? QString()
                                 : hashToHex(event->repository_status_revision.revision_staged);
            break;
        case LORE_EVENT_REPOSITORY_STATUS_FILE:
            ++reportedFileCount;
            break;
        case LORE_EVENT_REVISION_COMMIT_REVISION:
            committedRevision = hashToHex(event->revision_commit_revision.revision);
            checkpoints.append(core::Checkpoint{committedRevision, QString(), QDateTime()});
            break;
        case LORE_EVENT_REVISION_HISTORY_ENTRY:
            checkpoints.append(
                core::Checkpoint{hashToHex(event->revision_history_entry.revision), QString(),
                                 QDateTime()});
            break;
        case LORE_EVENT_METADATA:
            applyMetadata(event->metadata);
            break;
        default:
            break;
        }
    }

    // LORE reports a revision's message and timestamp as metadata events that
    // follow the revision they describe.
    void applyMetadata(const lore_metadata_event_data_t &metadata)
    {
        if (checkpoints.isEmpty()) {
            return;
        }
        core::Checkpoint &checkpoint = checkpoints.last();
        const QString key = fromLoreString(metadata.key);
        if (key == QLatin1String("message") && metadata.value.tag == LORE_METADATA_STRING) {
            checkpoint.message = fromLoreString(metadata.value.string);
        } else if (key == QLatin1String("timestamp")
                   && metadata.value.tag == LORE_METADATA_NUMERIC) {
            checkpoint.createdAtUtc = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(metadata.value.numeric), Qt::UTC);
        }
    }

    QMutex m_mutex;
};

/// Maps a LORE failure onto the stable pimio error vocabulary.
///
/// LORE's numeric codes are not a published contract yet, so the numeric code
/// is preserved in the error context and only well-understood messages are
/// promoted to a specific code.
ErrorCode mapFailure(const Operation &operation)
{
    const QString message = operation.message.toLower();
    if (message.contains(QLatin1String("nothing staged"))) {
        return ErrorCode::Conflict;
    }
    if (message.contains(QLatin1String("permission"))
        || message.contains(QLatin1String("access is denied"))
        || message.contains(QLatin1String("read-only"))) {
        return ErrorCode::PermissionDenied;
    }
    if (message.contains(QLatin1String("no space"))
        || message.contains(QLatin1String("disk full"))) {
        return ErrorCode::OutOfSpace;
    }
    return ErrorCode::Internal;
}

QJsonObject failureContext(const Operation &operation, const QString &call)
{
    QJsonObject context;
    context.insert(QStringLiteral("loreCall"), call);
    context.insert(QStringLiteral("loreStatus"), operation.status);
    return context;
}

/// LORE resolves relative paths against the process working directory, so the
/// adapter always passes absolute native paths.
QByteArray nativePath(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(path)).toUtf8();
}

/// Maps a media id onto a file name.
///
/// Ids are opaque, so anything outside a conservative set is hex-encoded. The
/// accepted set is deliberately lowercase-only: LORE rejects a tree that holds
/// two names differing only by case, and APFS and NTFS are case-insensitive by
/// default, so two ids that differ only by case must not produce two names.
/// Hex encoding is lowercase, so the mapping stays collision-free and
/// deterministic in both directions. Ids are read back from file contents
/// rather than from names.
QString recordFileName(const core::MediaId &id)
{
    static const QRegularExpression safe(QStringLiteral("\\A[a-z0-9._-]{1,64}\\z"));
    const QString value = id.value();
    if (safe.match(value).hasMatch() && !value.startsWith(QLatin1Char('.'))) {
        return value + QStringLiteral(".json");
    }
    return QStringLiteral("x-") + QString::fromLatin1(value.toUtf8().toHex())
           + QStringLiteral(".json");
}

/// One shard level keeps a large library from putting every record in a single
/// directory, which several filesystems handle poorly.
QString shardFor(const QString &fileName)
{
    QString shard = fileName.left(kShardNameLength);
    while (shard.size() < kShardNameLength) {
        shard.append(QLatin1Char('_'));
    }
    return shard;
}

bool writeRecordFile(const QString &path, const core::MediaRecord &record, Error *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create the directory %1.").arg(info.absolutePath()));
        return false;
    }

    // QSaveFile writes to a temporary file and renames it into place, so a
    // crash mid-write cannot leave a half-written record behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not open %1 for writing: %2").arg(path, file.errorString()));
        return false;
    }
    const QJsonDocument document(record.toJson());
    const QByteArray bytes = document.toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, ErrorCode::OutOfSpace,
                 QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

std::optional<core::MediaRecord> readRecordFile(const QString &path, Error *error)
{
    QFile file(path);
    if (!file.exists()) {
        setError(error, ErrorCode::NotFound, QStringLiteral("No record at %1.").arg(path));
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not read %1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The record at %1 is not valid JSON: %2")
                     .arg(path, parseError.errorString()));
        return std::nullopt;
    }
    return core::MediaRecord::fromJson(document.object());
}

// Zero-length pending level markers inside LORE's local store.
//
// LORE writes a fixed-size record into `.lore/{immutable,mutable}/index/<group>/
// level.pending` while promoting a level. A process killed between creating the
// file and writing the record leaves it empty, and every later open fails with
// "failed to fill whole buffer" while trying to read it back. An empty marker
// records no transition, so removing it is equivalent to the transition never
// having started, and the committed revisions become reachable again.
QStringList emptyPendingMarkers(const QString &repositoryPath)
{
    QStringList markers;
    const QString root = repositoryPath + QStringLiteral("/.lore");
    if (!QFileInfo::exists(root)) {
        return markers;
    }
    QDirIterator iterator(root, QStringList{QStringLiteral("level.pending")},
                          QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (QFileInfo(path).size() == 0) {
            markers.append(path);
        }
    }
    markers.sort();
    return markers;
}

/// Repeats a filesystem step that a healthy system can still refuse briefly.
///
/// Windows removes a directory entry only once the last handle to it closes and
/// lets a scanner hold a file that was just written, so a delete or a rename can
/// fail immediately after the step that made it possible succeeded. The store's
/// recovery path is the place where that matters: reporting it as a fault turns
/// a transient refusal into an unopenable library. Retrying costs nothing when
/// the first attempt works, which is every attempt on Linux and macOS.
template <typename Step>
bool withTransientRetries(Step step)
{
    constexpr int kAttempts = 25;
    constexpr unsigned long kDelayMs = 20;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (step()) {
            return true;
        }
        QThread::msleep(kDelayMs);
    }
    return step();
}

bool removeDirectoryContents(const QString &path)
{
    QDir directory(path);
    if (!directory.exists()) {
        return true;
    }
    bool removed = directory.removeRecursively();
    removed = QDir().mkpath(path) && removed;
    return removed;
}

bool copyDirectory(const QString &sourcePath, const QString &targetPath)
{
    if (!removeDirectoryContents(targetPath) || !QDir().mkpath(targetPath)) {
        return false;
    }

    const QDir source(sourcePath);
    QDirIterator iterator(sourcePath, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceFilePath = iterator.next();
        const QString targetFilePath = targetPath + QLatin1Char('/')
                                       + source.relativeFilePath(sourceFilePath);
        const QFileInfo sourceFile(sourceFilePath);
        if (sourceFile.isDir()) {
            if (!QDir().mkpath(targetFilePath)) {
                return false;
            }
        } else if (sourceFile.fileName() == QLatin1String("lock")) {
            // LORE recreates these transient files when it opens the restored store.
            continue;
        } else if (!QDir().mkpath(QFileInfo(targetFilePath).absolutePath())
                   || !QFile::copy(sourceFilePath, targetFilePath)) {
            return false;
        }
    }
    return true;
}

} // namespace

class LoreDurableStore::Private
{
public:
    explicit Private(QString storePath)
        : storePath(std::move(storePath))
    {
    }

    QString repositoryPath() const { return storePath + QStringLiteral("/repository"); }
    QString recordsPath() const { return repositoryPath() + QStringLiteral("/records"); }
    QString stagingPath() const { return storePath + QStringLiteral("/staging"); }
    QString lorePath() const { return repositoryPath() + QStringLiteral("/.lore"); }
    QString commitBackupPath() const { return storePath + QStringLiteral("/.pimio-lore-backup"); }
    QString commitMarkerPath() const
    {
        return storePath + QStringLiteral("/.pimio-lore-commit-in-progress");
    }

    QString committedRecordPath(const core::MediaId &id) const
    {
        const QString fileName = recordFileName(id);
        return recordsPath() + QLatin1Char('/') + shardFor(fileName) + QLatin1Char('/') + fileName;
    }

    QString stagedRecordPath(const core::MediaId &id) const
    {
        const QString fileName = recordFileName(id);
        return stagingPath() + QLatin1Char('/') + shardFor(fileName) + QLatin1Char('/') + fileName;
    }

    /// A tombstone in the staging area marks a committed record for deletion
    /// on the next commit. It uses a ".tombstone" extension so it is ignored
    /// by the record reader and clearly distinct from staged additions.
    QString stagedTombstonePath(const core::MediaId &id) const
    {
        const QString baseName = recordFileName(id);
        // Strip ".json" and append ".tombstone".
        const QString fileName = baseName.left(baseName.size() - 5) + QStringLiteral(".tombstone");
        return stagingPath() + QLatin1Char('/') + shardFor(baseName) + QLatin1Char('/') + fileName;
    }

    lore_global_args_t globals() const
    {
        lore_global_args_t args;
        std::memset(&args, 0, sizeof(args));
        args.repository_path = loreString(repositoryPathUtf8);
        // pimio's store is local-first: no operation may contact a server.
        args.offline = 1;
        args.local = 1;
        args.sync_data = 1;
        return args;
    }

    bool available() const { return opened && LoreApi::instance().isLoaded(); }

    /// Restores the checkout to the current committed revision.
    ///
    /// Used both to recover from an interrupted commit and to roll back a
    /// commit that failed part way through. Staged records live outside the
    /// checkout, so this never destroys uncommitted pimio work.
    bool restoreCheckoutToCommittedState(Error *error);
    bool recoverInterruptedCommit(Error *error);
    bool prepareCommitRecovery(Error *error);
    bool clearCommitRecovery();

    QString queryStateToken(Error *error) const;
    bool runStatus(Operation &operation, bool checkDirty, Error *error) const;

    QString storePath;
    QByteArray repositoryPathUtf8;
    bool opened = false;
    bool repairedOnOpen = false;
    std::unique_ptr<QLockFile> writerLock;
};

bool LoreDurableStore::Private::runStatus(Operation &operation, bool checkDirty, Error *error) const
{
    LoreApi &api = LoreApi::instance();
    const lore_global_args_t args = globals();

    lore_repository_status_args_t status;
    std::memset(&status, 0, sizeof(status));
    status.staged = 1;
    status.check_dirty = checkDirty ? 1 : 0;

    api.repositoryStatus(&args, &status, operation.config());
    if (operation.status != 0) {
        setError(error, mapFailure(operation),
                 QStringLiteral("Could not read the repository status: %1").arg(operation.message),
                 failureContext(operation, QStringLiteral("repository_status")));
        return false;
    }
    return true;
}

QString LoreDurableStore::Private::queryStateToken(Error *error) const
{
    Operation operation;
    if (!runStatus(operation, false, error)) {
        return {};
    }
    if (!operation.sawStatusRevision) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The repository reported no revision."));
        return {};
    }
    return QStringLiteral("lore:") + operation.currentRevision;
}

bool LoreDurableStore::Private::restoreCheckoutToCommittedState(Error *error)
{
    LoreApi &api = LoreApi::instance();
    const lore_global_args_t args = globals();

    const QByteArray records = nativePath(recordsPath());
    const lore_string_t path = loreString(records);

    // A staged node cannot be reset, so staging is always released first.
    Operation unstage;
    lore_file_unstage_args_t unstageArgs;
    std::memset(&unstageArgs, 0, sizeof(unstageArgs));
    unstageArgs.paths.ptr = &path;
    unstageArgs.paths.count = 1;
    api.fileUnstage(&args, &unstageArgs, unstage.config());

    Operation reset;
    lore_file_reset_args_t resetArgs;
    std::memset(&resetArgs, 0, sizeof(resetArgs));
    resetArgs.paths.ptr = &path;
    resetArgs.paths.count = 1;
    // Untracked files in the checkout are, by definition, the residue of a
    // commit that did not complete.
    resetArgs.purge = 1;
    api.fileReset(&args, &resetArgs, reset.config());

    if (reset.status != 0) {
        setError(error, mapFailure(reset),
                 QStringLiteral("Could not restore the checkout: %1").arg(reset.message),
                 failureContext(reset, QStringLiteral("file_reset")));
        return false;
    }
    return true;
}

bool LoreDurableStore::Private::recoverInterruptedCommit(Error *error)
{
    if (!QFileInfo::exists(commitMarkerPath())) {
        return true;
    }
    if (!QFileInfo::exists(commitBackupPath())) {
        setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The interrupted-commit backup is missing at %1.")
                     .arg(commitBackupPath()));
        return false;
    }
    if (!withTransientRetries([this] { return QDir(lorePath()).removeRecursively(); })
        || !withTransientRetries(
            [this] { return QDir().rename(commitBackupPath(), lorePath()); })
        || !withTransientRetries([this] { return QFile::remove(commitMarkerPath()); })) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not restore the repository after an interrupted commit."));
        return false;
    }
    repairedOnOpen = true;
    return true;
}

bool LoreDurableStore::Private::prepareCommitRecovery(Error *error)
{
    const bool backedUp =
        withTransientRetries([this] { return copyDirectory(lorePath(), commitBackupPath()); });
    if (!backedUp) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not back up the repository before committing."));
        return false;
    }

    QSaveFile marker(commitMarkerPath());
    if (!marker.open(QIODevice::WriteOnly)
        || marker.write(kCommitMarkerContents) != qint64(sizeof(kCommitMarkerContents) - 1)
        || !marker.commit()) {
        removeDirectoryContents(commitBackupPath());
        setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not mark the repository commit as in progress."));
        return false;
    }
    return true;
}

bool LoreDurableStore::Private::clearCommitRecovery()
{
    // The marker goes first and its removal is the part that matters: while it
    // exists, the next open rolls the repository back to the backup taken
    // before this commit. A commit that is already durable must never be undone
    // because a scanner held the marker open for a moment.
    const bool markerRemoved =
        withTransientRetries([this] { return !QFileInfo::exists(commitMarkerPath())
                                             || QFile::remove(commitMarkerPath()); });
    withTransientRetries([this] { return QDir(commitBackupPath()).removeRecursively(); });
    return markerRemoved;
}

LoreDurableStore::LoreDurableStore(QString storePath)
    : d(std::make_unique<Private>(std::move(storePath)))
{
}

LoreDurableStore::~LoreDurableStore()
{
    close();
}

const QString &LoreDurableStore::storePath() const
{
    return d->storePath;
}

QString LoreDurableStore::repositoryPath() const
{
    return d->repositoryPath();
}

bool LoreDurableStore::open(Error *error)
{
    if (d->opened) {
        return true;
    }

    LoreApi &api = LoreApi::instance();
    if (!api.isLoaded()) {
        setError(error, ErrorCode::StorageUnavailable, api.loadError());
        return false;
    }

    if (!QDir().mkpath(d->repositoryPath()) || !QDir().mkpath(d->stagingPath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create the store directories under %1.")
                     .arg(d->storePath));
        return false;
    }

    d->repositoryPathUtf8 = nativePath(d->repositoryPath());

    // Exactly one process may write a library. LORE 0.8.5 does not serialise
    // concurrent writers safely on its own: two processes committing at once
    // can leave its local store needing repair, so pimio keeps them apart
    // instead of discovering the damage afterwards. A second process gets a
    // clear conflict rather than a corrupt library.
    d->writerLock = std::make_unique<QLockFile>(d->storePath + QStringLiteral("/.pimio-writer.lock"));
    d->writerLock->setStaleLockTime(0);
    if (!d->writerLock->tryLock(0)) {
        qint64 pid = 0;
        QString host;
        QString application;
        d->writerLock->getLockInfo(&pid, &host, &application);
        QJsonObject context;
        context.insert(QStringLiteral("lockPid"), pid);
        context.insert(QStringLiteral("lockHost"), host);
        d->writerLock.reset();
        setError(error, ErrorCode::Conflict,
                 QStringLiteral("Another process is already using the library at %1.")
                     .arg(d->storePath),
                 context);
        return false;
    }

    if (!d->recoverInterruptedCommit(error)) {
        d->writerLock.reset();
        return false;
    }

    // Now that no other writer can be active, an empty pending marker in
    // LORE's local store can only be residue from a killed process, so it is
    // safe to clear automatically. The flag keeps the recovery visible.
    d->repairedOnOpen = false;
    if (needsRepairAfterInterruptedWrite()) {
        if (!repairAfterInterruptedWrite(error)) {
            d->writerLock.reset();
            return false;
        }
        d->repairedOnOpen = true;
    }

    const bool existing = QFileInfo::exists(d->lorePath());
    if (!existing) {
        const lore_global_args_t args = d->globals();
        lore_repository_create_args_t createArgs;
        std::memset(&createArgs, 0, sizeof(createArgs));
        // A local-only repository still needs a syntactically valid URL. It is
        // never contacted because every call runs with offline and local set.
        const QByteArray url = QByteArrayLiteral("lore://localhost/pimio");
        createArgs.repository_url = loreString(url);

        Operation create;
        api.repositoryCreate(&args, &createArgs, create.config());
        if (create.status != 0) {
            setError(error, mapFailure(create),
                     QStringLiteral("Could not create the LORE repository at %1: %2")
                         .arg(d->repositoryPath(), create.message),
                     failureContext(create, QStringLiteral("repository_create")));
            return false;
        }
    }

    if (!QDir().mkpath(d->recordsPath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create %1.").arg(d->recordsPath()));
        return false;
    }

    d->opened = true;

    // Recovery. A commit that was interrupted after records were copied into
    // the checkout but before LORE committed them leaves the checkout ahead of
    // history. Restoring it is what keeps load() from reporting an uncommitted
    // change as committed; the staging area still holds the work, so nothing
    // is lost.
    //
    // This runs unconditionally rather than only when a cheap status reports a
    // difference: an interrupted commit leaves untracked files, and untracked
    // files are invisible to a status that does not walk the tree. Paying a
    // full restore once per open is the cost of the invariant.
    if (!d->restoreCheckoutToCommittedState(error)) {
        d->opened = false;
        d->writerLock.reset();
        return false;
    }

    return true;
}

void LoreDurableStore::close()
{
    if (!d->opened) {
        return;
    }
    LoreApi &api = LoreApi::instance();
    if (api.isLoaded()) {
        const lore_global_args_t args = d->globals();

        Operation flush;
        lore_repository_flush_args_t flushArgs;
        std::memset(&flushArgs, 0, sizeof(flushArgs));
        api.repositoryFlush(&args, &flushArgs, flush.config());

        Operation release;
        lore_repository_release_args_t releaseArgs;
        std::memset(&releaseArgs, 0, sizeof(releaseArgs));
        api.repositoryRelease(&args, &releaseArgs, release.config());
    }
    d->opened = false;
    d->writerLock.reset();
}

bool LoreDurableStore::repairedInterruptedWriteOnOpen() const
{
    return d->repairedOnOpen;
}

bool LoreDurableStore::isAvailable() const
{
    return d->available();
}

bool LoreDurableStore::needsRepairAfterInterruptedWrite() const
{
    return !emptyPendingMarkers(d->repositoryPath()).isEmpty();
}

bool LoreDurableStore::repairAfterInterruptedWrite(Error *error)
{
    const QStringList markers = emptyPendingMarkers(d->repositoryPath());
    if (markers.isEmpty()) {
        setError(error, ErrorCode::NotFound,
                 QStringLiteral("There is nothing to repair in the repository at %1.")
                     .arg(d->repositoryPath()));
        return false;
    }
    for (const QString &marker : markers) {
        if (!QFile::remove(marker)) {
            setError(error, ErrorCode::PermissionDenied,
                     QStringLiteral("Could not remove the empty pending marker %1.").arg(marker));
            return false;
        }
    }
    return true;
}

bool LoreDurableStore::restoreFromDurableState(Error *error)
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!QDir().mkpath(d->recordsPath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create %1.").arg(d->recordsPath()));
        return false;
    }
    return d->restoreCheckoutToCommittedState(error);
}

bool LoreDurableStore::stage(const core::MediaRecord &record, Error *error)
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!record.id.isValid()) {
        setError(error, ErrorCode::Internal,
                 QStringLiteral("A record must have a valid media id."));
        return false;
    }
    return writeRecordFile(d->stagedRecordPath(record.id), record, error);
}

bool LoreDurableStore::hasStagedChanges() const
{
    if (!d->available()) {
        return false;
    }
    QDirIterator iterator(
        d->stagingPath(),
        QStringList{QStringLiteral("*.json"), QStringLiteral("*.tombstone")},
        QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    return iterator.hasNext();
}


std::optional<core::Checkpoint> LoreDurableStore::commit(const QString &message, Error *error)
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return std::nullopt;
    }

    QStringList stagedFiles;
    QStringList tombstoneFiles;
    {
        QDirIterator iterator(
            d->stagingPath(),
            QStringList{QStringLiteral("*.json"), QStringLiteral("*.tombstone")},
            QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (path.endsWith(QLatin1String(".tombstone"))) {
                tombstoneFiles.append(path);
            } else {
                stagedFiles.append(path);
            }
        }
    }
    if (stagedFiles.isEmpty() && tombstoneFiles.isEmpty()) {
        setError(error, ErrorCode::Conflict,
                 QStringLiteral("There are no staged changes to commit."));
        return std::nullopt;
    }
    std::sort(stagedFiles.begin(), stagedFiles.end());
    std::sort(tombstoneFiles.begin(), tombstoneFiles.end());

    // Apply tombstone deletions to the checkout first so that fileStage(scan=1)
    // picks them up as removals.
    const QDir staging(d->stagingPath());
    const QString records = d->recordsPath();
    for (const QString &tombstone : std::as_const(tombstoneFiles)) {
        const QString relative = staging.relativeFilePath(tombstone);
        // Replace the ".tombstone" suffix with ".json" to get the record path.
        const QString recordRelative =
            relative.left(relative.size() - qsizetype(sizeof(".tombstone") - 1))
            + QStringLiteral(".json");
        QFile::remove(records + QLatin1Char('/') + recordRelative);
    }

    for (const QString &stagedFile : std::as_const(stagedFiles)) {
        const QString relative = staging.relativeFilePath(stagedFile);
        const QString target = records + QLatin1Char('/') + relative;
        const QFileInfo targetInfo(target);
        if (!QDir().mkpath(targetInfo.absolutePath())) {
            d->restoreCheckoutToCommittedState(nullptr);
            setError(error, ErrorCode::PermissionDenied,
                     QStringLiteral("Could not create %1.").arg(targetInfo.absolutePath()));
            return std::nullopt;
        }
        QFile::remove(target);
        if (!QFile::copy(stagedFile, target)) {
            // Roll back so the checkout never holds a partially applied batch.
            d->restoreCheckoutToCommittedState(nullptr);
            setError(error, ErrorCode::OutOfSpace,
                     QStringLiteral("Could not copy the staged record %1 into the checkout.")
                         .arg(relative));
            return std::nullopt;
        }
    }

    LoreApi &api = LoreApi::instance();
    const lore_global_args_t args = d->globals();
    const QByteArray recordsPath = nativePath(records);
    const lore_string_t path = loreString(recordsPath);

    Operation stageOperation;
    lore_file_stage_args_t stageArgs;
    std::memset(&stageArgs, 0, sizeof(stageArgs));
    stageArgs.paths.ptr = &path;
    stageArgs.paths.count = 1;
    stageArgs.scan = 1;
    api.fileStage(&args, &stageArgs, stageOperation.config());
    if (stageOperation.status != 0) {
        d->restoreCheckoutToCommittedState(nullptr);
        setError(error, mapFailure(stageOperation),
                 QStringLiteral("Could not stage the records: %1").arg(stageOperation.message),
                 failureContext(stageOperation, QStringLiteral("file_stage")));
        return std::nullopt;
    }

    Operation commitOperation;
    lore_revision_commit_args_t commitArgs;
    std::memset(&commitArgs, 0, sizeof(commitArgs));
    const QByteArray messageUtf8 = message.toUtf8();
    commitArgs.message = loreString(messageUtf8);
    if (!d->prepareCommitRecovery(error)) {
        d->restoreCheckoutToCommittedState(nullptr);
        return std::nullopt;
    }
    api.revisionCommit(&args, &commitArgs, commitOperation.config());
    if (commitOperation.status != 0 || commitOperation.checkpoints.isEmpty()) {
        // The staging area is untouched, so the work is still recoverable and
        // the checkout is returned to the last committed revision.
        d->restoreCheckoutToCommittedState(nullptr);
        d->clearCommitRecovery();
        setError(error, mapFailure(commitOperation),
                 QStringLiteral("The commit failed: %1").arg(commitOperation.message),
                 failureContext(commitOperation, QStringLiteral("revision_commit")));
        return std::nullopt;
    }

    // LORE reports a revision as committed before it has written it out, so a
    // process that dies between here and close() loses a save it already
    // reported as durable. Flushing here is what makes the checkpoint this
    // function returns mean what it says.
    Operation flushOperation;
    lore_repository_flush_args_t flushArgs;
    std::memset(&flushArgs, 0, sizeof(flushArgs));
    api.repositoryFlush(&args, &flushArgs, flushOperation.config());
    if (flushOperation.status != 0) {
        // The backup and the in-progress marker are deliberately left in
        // place: the revision is not durable, so the next open must roll the
        // repository back rather than trust whatever reached the disk.
        d->restoreCheckoutToCommittedState(nullptr);
        setError(error, mapFailure(flushOperation),
                 QStringLiteral("The commit could not be made durable: %1")
                     .arg(flushOperation.message),
                 failureContext(flushOperation, QStringLiteral("repository_flush")));
        return std::nullopt;
    }

    core::Checkpoint checkpoint = commitOperation.checkpoints.constFirst();
    checkpoint.message = message;

    // The revision is durable, so the rollback path must be retired before the
    // staging area is touched. A kill between these two steps would otherwise
    // roll the commit back while its staged inputs were already half deleted,
    // which is the one outcome the store promises cannot happen.
    if (!d->clearCommitRecovery()) {
        // The revision is durable, so this is residue rather than a failed
        // save; it is reported because the next open would otherwise roll a
        // committed revision back.
        setError(error, ErrorCode::Internal,
                 QStringLiteral("The commit succeeded but the rollback marker at %1 could not be "
                                "removed.")
                     .arg(d->commitMarkerPath()));
        return checkpoint;
    }

    if (!removeDirectoryContents(d->stagingPath())) {
        // The commit itself succeeded; report the residue rather than claiming
        // the save failed.
        setError(error, ErrorCode::Internal,
                 QStringLiteral("The commit succeeded but the staging area could not be cleared."));
    }

    return checkpoint;
}

bool LoreDurableStore::discardStaged(Error *error)
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!removeDirectoryContents(d->stagingPath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not clear the staging area at %1.").arg(d->stagingPath()));
        return false;
    }
    // discardStaged also rolls back any checkout deletions made for tombstoned
    // records. Restoring the checkout brings deleted committed files back.
    return d->restoreCheckoutToCommittedState(error);
}

bool LoreDurableStore::remove(const core::MediaId &id, Error *error)
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    // Cancel any pending staged update for this record.
    QFile::remove(d->stagedRecordPath(id));

    // Write a tombstone so that commit() knows to remove this record from the
    // checkout. This is needed even if the record is not yet committed, because
    // a later re-stage before commit must not silently resurrect it.
    const QString tombstone = d->stagedTombstonePath(id);
    const QFileInfo tombstoneInfo(tombstone);
    if (!QDir().mkpath(tombstoneInfo.absolutePath())) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create staging directory for removal marker."));
        return false;
    }
    QFile marker(tombstone);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not write removal marker for %1.").arg(id.value()));
        return false;
    }
    return true;
}

std::optional<core::MediaRecord> LoreDurableStore::load(const core::MediaId &id,
                                                        Error *error) const
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return std::nullopt;
    }
    return readRecordFile(d->committedRecordPath(id), error);
}

QList<core::MediaId> LoreDurableStore::listIds(Error *error) const
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return {};
    }

    QStringList values;
    QDirIterator iterator(d->recordsPath(), QStringList{QStringLiteral("*.json")},
                          QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const auto record = readRecordFile(path, nullptr);
        if (record && record->id.isValid()) {
            values.append(record->id.value());
        }
    }

    std::sort(values.begin(), values.end());
    QList<core::MediaId> ids;
    ids.reserve(values.size());
    for (const QString &value : std::as_const(values)) {
        ids.append(core::MediaId(value));
    }
    return ids;
}

QList<core::Checkpoint> LoreDurableStore::history(int limit, Error *error) const
{
    if (!d->available()) {
        setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return {};
    }

    LoreApi &api = LoreApi::instance();
    const lore_global_args_t args = d->globals();

    lore_revision_history_args_t historyArgs;
    std::memset(&historyArgs, 0, sizeof(historyArgs));
    historyArgs.length = limit > 0 ? static_cast<uint32_t>(limit) : 0;

    Operation operation;
    api.revisionHistory(&args, &historyArgs, operation.config());
    if (operation.status != 0) {
        setError(error, mapFailure(operation),
                 QStringLiteral("Could not read the history: %1").arg(operation.message),
                 failureContext(operation, QStringLiteral("revision_history")));
        return {};
    }

    // LORE reports the newest revision first, which is the order the contract
    // requires.
    QList<core::Checkpoint> checkpoints = operation.checkpoints;
    if (limit >= 0 && limit < checkpoints.size()) {
        checkpoints = checkpoints.first(limit);
    }
    return checkpoints;
}

QString LoreDurableStore::stateToken() const
{
    if (!d->available()) {
        return {};
    }
    // Deliberately queried rather than cached: a change made by another
    // process must be visible here, because this token is what tells the
    // SQLite projection that it has to be rebuilt.
    return d->queryStateToken(nullptr);
}

} // namespace pimio::lore
