#include "lore_durable_store_private.h"

#include "pimio/core/version.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>
#include <cstring>

namespace pimio::lore {

using core::Error;
using core::ErrorCode;

bool LoreDurableStore::stage(const core::MediaRecord &record, Error *error)
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!record.id.isValid()) {
        detail::setError(error, ErrorCode::Internal,
                 QStringLiteral("A record must have a valid media id."));
        return false;
    }
    return detail::writeRecordFile(d->stagedRecordPath(record.id), record, error);
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
        detail::setError(error, ErrorCode::StorageUnavailable,
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
        detail::setError(error, ErrorCode::Conflict,
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
            detail::setError(error, ErrorCode::PermissionDenied,
                     QStringLiteral("Could not create %1.").arg(targetInfo.absolutePath()));
            return std::nullopt;
        }
        QFile::remove(target);
        if (!QFile::copy(stagedFile, target)) {
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, ErrorCode::OutOfSpace,
                     QStringLiteral("Could not copy the staged record %1 into the checkout.")
                         .arg(relative));
            return std::nullopt;
        }
    }

    LoreApi &api = LoreApi::instance();
    const lore_global_args_t args = d->globals();
    const QByteArray recordsPath = detail::nativePath(records);
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
        detail::setError(error, mapFailure(stageOperation),
                 QStringLiteral("Could not stage the records: %1").arg(stageOperation.message),
                 failureContext(stageOperation, QStringLiteral("file_stage")));
        return std::nullopt;
    }

    Operation commitOperation;
    lore_revision_commit_args_t commitArgs;
    std::memset(&commitArgs, 0, sizeof(commitArgs));
    core::LibraryDescriptor descriptor;
    {
        const QString descriptorPath = QFileInfo::exists(d->stagedLibraryDescriptorPath())
                ? d->stagedLibraryDescriptorPath()
                : d->libraryDescriptorPath();
        QFile descriptorFile(descriptorPath);
        if (descriptorFile.open(QIODevice::ReadOnly)) {
            descriptor = core::LibraryDescriptor::fromJson(
                    QJsonDocument::fromJson(descriptorFile.readAll()).object());
        }
    }
    const QList<core::Checkpoint> previous = history(1, nullptr);
    const QString parentId = previous.isEmpty() ? QString() : previous.constFirst().id;
    const QJsonObject provenance{
        {QStringLiteral("message"), message},
        {QStringLiteral("authorId"), descriptor.isValid()
                                             ? descriptor.localUser.id
                                             : QString(core::kUnknownAuthorId)},
        {QStringLiteral("applicationVersion"), core::versionString()},
        {QStringLiteral("parentId"), parentId},
    };
    const QByteArray messageUtf8 =
            QByteArrayLiteral("pimio-checkpoint-v1:")
            + QJsonDocument(provenance).toJson(QJsonDocument::Compact);
    commitArgs.message = loreString(messageUtf8);
    api.revisionCommit(&args, &commitArgs, commitOperation.config());
    if (commitOperation.status != 0 || commitOperation.checkpoints.isEmpty()) {
        d->restoreCheckoutToCommittedState(nullptr);
        detail::setError(error, mapFailure(commitOperation),
                 QStringLiteral("The commit failed: %1").arg(commitOperation.message),
                 failureContext(commitOperation, QStringLiteral("revision_commit")));
        return std::nullopt;
    }

    Operation flushOperation;
    lore_repository_flush_args_t flushArgs;
    std::memset(&flushArgs, 0, sizeof(flushArgs));
    api.repositoryFlush(&args, &flushArgs, flushOperation.config());
    if (flushOperation.status != 0) {
        d->restoreCheckoutToCommittedState(nullptr);
        detail::setError(error, mapFailure(flushOperation),
                 QStringLiteral("The commit could not be made durable: %1")
                     .arg(flushOperation.message),
                 failureContext(flushOperation, QStringLiteral("repository_flush")));
        return std::nullopt;
    }

    core::Checkpoint checkpoint = commitOperation.checkpoints.constFirst();
    checkpoint.message = message;
    checkpoint.authorId = provenance.value(QStringLiteral("authorId")).toString();
    checkpoint.applicationVersion = core::versionString();
    checkpoint.parentId = parentId;

    if (!detail::removeDirectoryContents(d->stagingPath())) {
        detail::setError(error, ErrorCode::Internal,
                 QStringLiteral("The commit succeeded but the staging area could not be cleared."));
    }

    return checkpoint;
}

bool LoreDurableStore::discardStaged(Error *error)
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!detail::removeDirectoryContents(d->stagingPath())) {
        detail::setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not clear the staging area at %1.").arg(d->stagingPath()));
        return false;
    }
    return d->restoreCheckoutToCommittedState(error);
}

bool LoreDurableStore::remove(const core::MediaId &id, Error *error)
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    QFile::remove(d->stagedRecordPath(id));

    const QString tombstone = d->stagedTombstonePath(id);
    const QFileInfo tombstoneInfo(tombstone);
    if (!QDir().mkpath(tombstoneInfo.absolutePath())) {
        detail::setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not create staging directory for removal marker."));
        return false;
    }
    QFile marker(tombstone);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        detail::setError(error, ErrorCode::PermissionDenied,
                 QStringLiteral("Could not write removal marker for %1.").arg(id.value()));
        return false;
    }
    return true;
}

} // namespace pimio::lore
