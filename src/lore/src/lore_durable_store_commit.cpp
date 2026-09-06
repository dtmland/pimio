#include "lore_durable_store_private.h"

#include "pimio/core/version.h"

#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>

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

bool LoreDurableStore::stageOriginal(const core::MediaRecord &record, const QString &sourcePath,
                                     Error *error)
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                         QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (record.originalStorage != core::MediaRecord::OriginalStorage::Managed
        || record.managedOriginalPath.isEmpty()) {
        detail::setError(error, ErrorCode::Internal,
                         QStringLiteral("A managed record needs a repository-relative path."));
        return false;
    }
    if (originalPath(record, error).isEmpty()) {
        return false;
    }

    const QString target = d->stagedOriginalPath(record);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        detail::setError(error, ErrorCode::PermissionDenied,
                         QStringLiteral("Could not create the original staging directory."));
        return false;
    }
    const QString stagingRoot = QFileInfo(d->stagedOriginalsPath()).canonicalFilePath();
    const QString stagingParent = QFileInfo(target).absoluteDir().canonicalPath();
    const QString stagingRelative =
            QDir::fromNativeSeparators(QDir(stagingRoot).relativeFilePath(stagingParent));
    if (stagingRoot.isEmpty() || stagingRelative == QLatin1String("..")
        || stagingRelative.startsWith(QLatin1String("../"))
        || QDir::isAbsolutePath(stagingRelative)) {
        detail::setError(error, ErrorCode::CorruptData,
                         QStringLiteral("The staged original resolves outside the staging area."));
        return false;
    }
    const QString temporary = target + QStringLiteral(".part");
    QFile::remove(temporary);
    if (!QFile::copy(sourcePath, temporary)) {
        detail::setError(error, ErrorCode::OutOfSpace,
                         QStringLiteral("Could not stage the original %1.").arg(sourcePath));
        return false;
    }

    QFile copied(temporary);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!copied.open(QIODevice::ReadOnly) || !hash.addData(&copied)
        || QString::fromLatin1(hash.result().toHex()) != record.fingerprint.digest()) {
        copied.close();
        QFile::remove(temporary);
        detail::setError(error, ErrorCode::CorruptData,
                         QStringLiteral("The staged original does not match its fingerprint."));
        return false;
    }
    copied.close();
    QFile::remove(target);
    if (!QFile::rename(temporary, target)) {
        QFile::remove(temporary);
        detail::setError(error, ErrorCode::PermissionDenied,
                         QStringLiteral("Could not publish the staged original."));
        return false;
    }
    if (!stage(record, error)) {
        QFile::remove(target);
        return false;
    }
    return true;
}

bool LoreDurableStore::hasStagedChanges() const
{
    if (!d->available()) {
        return false;
    }
    QDirIterator iterator(d->stagingPath(), QDir::Files | QDir::Hidden,
                      QDirIterator::Subdirectories);
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
    QStringList stagedOriginalFiles;
    bool originalsChanged = false;
    {
        QDirIterator iterator(d->stagingPath(), QDir::Files | QDir::Hidden,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QString relative = QDir(d->stagingPath()).relativeFilePath(path);
            if (relative.startsWith(QLatin1String("originals/"))) {
                stagedOriginalFiles.append(path);
            } else if (path.endsWith(QLatin1String(".tombstone"))) {
                tombstoneFiles.append(path);
            } else if (path.endsWith(QLatin1String(".json"))) {
                stagedFiles.append(path);
            }
        }
    }
    std::sort(stagedFiles.begin(), stagedFiles.end());
    std::sort(tombstoneFiles.begin(), tombstoneFiles.end());
    std::sort(stagedOriginalFiles.begin(), stagedOriginalFiles.end());

    QSet<QString> expectedOriginals;
    for (const QString &stagedFile : std::as_const(stagedFiles)) {
        const auto record = detail::readRecordFile(stagedFile, nullptr);
        if (record && record->originalStorage == core::MediaRecord::OriginalStorage::Managed
            && !record->managedOriginalPath.isEmpty()) {
            expectedOriginals.insert(record->managedOriginalPath);
        }
    }
    for (auto it = stagedOriginalFiles.begin(); it != stagedOriginalFiles.end();) {
        const QString relative = QDir(d->stagingPath()).relativeFilePath(*it);
        if (relative.endsWith(QLatin1String(".part")) || !expectedOriginals.contains(relative)) {
            if (!QFile::remove(*it)) {
                detail::setError(error, ErrorCode::PermissionDenied,
                                 QStringLiteral("Could not clear an orphaned staged original."));
                return std::nullopt;
            }
            it = stagedOriginalFiles.erase(it);
        } else {
            ++it;
        }
    }
    if (stagedFiles.isEmpty() && tombstoneFiles.isEmpty()) {
        detail::setError(error, ErrorCode::Conflict,
                         QStringLiteral("There are no staged changes to commit."));
        return std::nullopt;
    }
    for (const QString &stagedFile : std::as_const(stagedFiles)) {
        const auto record = detail::readRecordFile(stagedFile, nullptr);
        if (!record || record->originalStorage != core::MediaRecord::OriginalStorage::Managed) {
            continue;
        }
        const auto committed = detail::readRecordFile(d->committedRecordPath(record->id), nullptr);
        const bool keepsCommittedOriginal =
                committed
                && committed->originalStorage == core::MediaRecord::OriginalStorage::Managed
                && committed->managedOriginalPath == record->managedOriginalPath
                && QFileInfo::exists(d->committedOriginalPath(*committed));
        if (!QFileInfo::exists(d->stagedOriginalPath(*record)) && !keepsCommittedOriginal) {
            detail::setError(error, ErrorCode::CorruptData,
                             QStringLiteral("A staged managed record has no matching original."));
            return std::nullopt;
        }
    }

    // Apply tombstone deletions to the checkout first so that fileStage(scan=1)
    // picks them up as removals.
    const QDir staging(d->stagingPath());
    const QString records = d->recordsPath();
    for (const QString &tombstone : std::as_const(tombstoneFiles)) {
        const QString relative = staging.relativeFilePath(tombstone);
        const QString recordRelative =
            relative.left(relative.size() - qsizetype(sizeof(".tombstone") - 1))
            + QStringLiteral(".json");
        const QString committedRecord = records + QLatin1Char('/') + recordRelative;
        const auto record = detail::readRecordFile(committedRecord, nullptr);
        if (record && record->originalStorage == core::MediaRecord::OriginalStorage::Managed) {
            const QString original = d->committedOriginalPath(*record);
            if (QFileInfo::exists(original) && !QFile::remove(original)) {
                d->restoreCheckoutToCommittedState(nullptr);
                detail::setError(error, ErrorCode::PermissionDenied,
                                 QStringLiteral("Could not remove a managed original."));
                return std::nullopt;
            }
            originalsChanged = true;
        }
        QFile::remove(committedRecord);
    }

    for (const QString &stagedOriginal : std::as_const(stagedOriginalFiles)) {
        originalsChanged = true;
        const QString relative = staging.relativeFilePath(stagedOriginal);
        const QString target = d->repositoryPath() + QLatin1Char('/') + relative;
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, ErrorCode::PermissionDenied,
                             QStringLiteral("Could not create the managed-original directory."));
            return std::nullopt;
        }
        const QString originalsRoot = QFileInfo(d->originalsPath()).canonicalFilePath();
        const QString targetParent = QFileInfo(target).absoluteDir().canonicalPath();
        const QString targetRelative =
                QDir::fromNativeSeparators(QDir(originalsRoot).relativeFilePath(targetParent));
        if (originalsRoot.isEmpty() || targetRelative == QLatin1String("..")
            || targetRelative.startsWith(QLatin1String("../"))
            || QDir::isAbsolutePath(targetRelative)) {
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, ErrorCode::CorruptData,
                             QStringLiteral("A managed original resolves outside the repository."));
            return std::nullopt;
        }
        if (QFileInfo::exists(target)) {
            continue;
        }
        const QString temporary = target + QStringLiteral(".pimio-write");
        QFile::remove(temporary);
        if (!QFile::copy(stagedOriginal, temporary)) {
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, ErrorCode::OutOfSpace,
                             QStringLiteral("Could not copy a staged original into the checkout."));
            return std::nullopt;
        }
        if (!QFile::rename(temporary, target)) {
            QFile::remove(temporary);
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, ErrorCode::PermissionDenied,
                             QStringLiteral("Could not publish a managed original."));
            return std::nullopt;
        }
    }

    for (const QString &stagedFile : std::as_const(stagedFiles)) {
        const auto record = detail::readRecordFile(stagedFile, nullptr);
        if (!record || record->originalStorage != core::MediaRecord::OriginalStorage::Managed) {
            continue;
        }
        const auto committed = detail::readRecordFile(d->committedRecordPath(record->id), nullptr);
        if (!committed
            || committed->originalStorage != core::MediaRecord::OriginalStorage::Managed
            || committed->managedOriginalPath == record->managedOriginalPath) {
            continue;
        }
        const QString previousOriginal = d->committedOriginalPath(*committed);
        if (QFileInfo::exists(previousOriginal) && !QFile::remove(previousOriginal)) {
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, ErrorCode::PermissionDenied,
                             QStringLiteral("Could not replace a managed original."));
            return std::nullopt;
        }
        originalsChanged = true;
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
    auto stageTree = [&](const QString &tree, Operation &operation) {
        const QByteArray nativePath = detail::nativePath(tree);
        const lore_string_t path = loreString(nativePath);
        lore_file_stage_args_t stageArgs;
        std::memset(&stageArgs, 0, sizeof(stageArgs));
        stageArgs.paths.ptr = &path;
        stageArgs.paths.count = 1;
        stageArgs.scan = 1;
        api.fileStage(&args, &stageArgs, operation.config());
    };

    if (originalsChanged) {
        Operation originalStageOperation;
        stageTree(d->originalsPath(), originalStageOperation);
        if (originalStageOperation.status != 0) {
            d->restoreCheckoutToCommittedState(nullptr);
            detail::setError(error, mapFailure(originalStageOperation),
                             QStringLiteral("Could not stage managed originals: %1")
                                     .arg(originalStageOperation.message),
                             failureContext(originalStageOperation,
                                            QStringLiteral("file_stage")));
            return std::nullopt;
        }
    }

    Operation stageOperation;
    stageTree(records, stageOperation);
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
    const QString stagedRecordPath = d->stagedRecordPath(id);
    const auto stagedRecord = detail::readRecordFile(stagedRecordPath, nullptr);
    if (stagedRecord
        && stagedRecord->originalStorage == core::MediaRecord::OriginalStorage::Managed) {
        QFile::remove(d->stagedOriginalPath(*stagedRecord));
    }
    QFile::remove(stagedRecordPath);

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
