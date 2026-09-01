#include "lore_durable_store_private.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace pimio::lore {

using core::Error;
using core::ErrorCode;

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
        detail::setError(error, mapFailure(operation),
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
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The repository reported no revision."));
        return {};
    }
    return QStringLiteral("lore:") + operation.currentRevision;
}

bool LoreDurableStore::Private::restoreCheckoutToCommittedState(Error *error)
{
    LoreApi &api = LoreApi::instance();
    const lore_global_args_t args = globals();

    const QByteArray records = detail::nativePath(recordsPath());
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
        detail::setError(error, mapFailure(reset),
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
        detail::setError(error, ErrorCode::CorruptData,
                 QStringLiteral("The interrupted-commit backup is missing at %1.")
                     .arg(commitBackupPath()));
        return false;
    }
    if (!detail::withTransientRetries([this] { return QDir(lorePath()).removeRecursively(); })
        || !detail::withTransientRetries(
            [this] { return QDir().rename(commitBackupPath(), lorePath()); })
        || !detail::withTransientRetries([this] { return QFile::remove(commitMarkerPath()); })) {
        detail::setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not restore the repository after an interrupted commit."));
        return false;
    }
    repairedOnOpen = true;
    return true;
}

bool LoreDurableStore::Private::prepareCommitRecovery(Error *error)
{
    const bool backedUp =
        detail::withTransientRetries([this] { return detail::copyDirectory(lorePath(), commitBackupPath()); });
    if (!backedUp) {
        detail::setError(error, ErrorCode::Internal,
                 QStringLiteral("Could not back up the repository before committing."));
        return false;
    }

    QSaveFile marker(commitMarkerPath());
    if (!marker.open(QIODevice::WriteOnly)
        || marker.write(kCommitMarkerContents) != qint64(sizeof(kCommitMarkerContents) - 1)
        || !marker.commit()) {
        detail::removeDirectoryContents(commitBackupPath());
        detail::setError(error, ErrorCode::Internal,
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
        detail::withTransientRetries([this] { return !QFileInfo::exists(commitMarkerPath())
                                             || QFile::remove(commitMarkerPath()); });
    detail::withTransientRetries([this] { return QDir(commitBackupPath()).removeRecursively(); });
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
        detail::setError(error, ErrorCode::StorageUnavailable, api.loadError());
        return false;
    }

    if (!QDir().mkpath(d->repositoryPath()) || !QDir().mkpath(d->stagingPath())) {
        detail::setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create the store directories under %1.")
                     .arg(d->storePath));
        return false;
    }

    d->repositoryPathUtf8 = detail::nativePath(d->repositoryPath());

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
        detail::setError(error, ErrorCode::Conflict,
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
            detail::setError(error, mapFailure(create),
                     QStringLiteral("Could not create the LORE repository at %1: %2")
                         .arg(d->repositoryPath(), create.message),
                     failureContext(create, QStringLiteral("repository_create")));
            return false;
        }
    }

    if (!QDir().mkpath(d->recordsPath())) {
        detail::setError(error, ErrorCode::PermissionDenied,
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
    return !detail::emptyPendingMarkers(d->repositoryPath()).isEmpty();
}

bool LoreDurableStore::repairAfterInterruptedWrite(Error *error)
{
    const QStringList markers = detail::emptyPendingMarkers(d->repositoryPath());
    if (markers.isEmpty()) {
        detail::setError(error, ErrorCode::NotFound,
                 QStringLiteral("There is nothing to repair in the repository at %1.")
                     .arg(d->repositoryPath()));
        return false;
    }
    for (const QString &marker : markers) {
        if (!QFile::remove(marker)) {
            detail::setError(error, ErrorCode::PermissionDenied,
                     QStringLiteral("Could not remove the empty pending marker %1.").arg(marker));
            return false;
        }
    }
    return true;
}

bool LoreDurableStore::restoreFromDurableState(Error *error)
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!QDir().mkpath(d->recordsPath())) {
        detail::setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create %1.").arg(d->recordsPath()));
        return false;
    }
    return d->restoreCheckoutToCommittedState(error);
}

std::optional<core::MediaRecord> LoreDurableStore::load(const core::MediaId &id,
                                                        Error *error) const
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return std::nullopt;
    }
    return detail::readRecordFile(d->committedRecordPath(id), error);
}

QList<core::MediaId> LoreDurableStore::listIds(Error *error) const
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return {};
    }

    QStringList values;
    QDirIterator iterator(d->recordsPath(), QStringList{QStringLiteral("*.json")},
                          QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (QFileInfo(path).fileName() == QLatin1String(".pimio-library.json")) {
            continue;
        }
        const auto record = detail::readRecordFile(path, nullptr);
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
        detail::setError(error, ErrorCode::StorageUnavailable,
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
        detail::setError(error, mapFailure(operation),
                 QStringLiteral("Could not read the history: %1").arg(operation.message),
                 failureContext(operation, QStringLiteral("revision_history")));
        return {};
    }

    // LORE reports the newest revision first, which is the order the contract
    // requires.
    QList<core::Checkpoint> checkpoints = operation.checkpoints;
    for (core::Checkpoint &checkpoint : checkpoints) {
        constexpr QLatin1StringView prefix{"pimio-checkpoint-v1:"};
        if (!checkpoint.message.startsWith(prefix)) {
            checkpoint.authorId = QString(core::kUnknownAuthorId);
            continue;
        }
        const QByteArray encoded = checkpoint.message.sliced(prefix.size()).toUtf8();
        const QJsonDocument document = QJsonDocument::fromJson(encoded);
        if (!document.isObject()) {
            checkpoint.authorId = QString(core::kUnknownAuthorId);
            continue;
        }
        const QJsonObject provenance = document.object();
        checkpoint.message = provenance.value(QStringLiteral("message")).toString();
        checkpoint.authorId = provenance.value(QStringLiteral("authorId"))
                                      .toString(QString(core::kUnknownAuthorId));
        checkpoint.applicationVersion =
                provenance.value(QStringLiteral("applicationVersion")).toString();
        checkpoint.parentId = provenance.value(QStringLiteral("parentId")).toString();
    }
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
