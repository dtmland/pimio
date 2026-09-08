#include "pimio/scan/scanner.h"

#include "pimio/core/metadata.h"
#include "pimio/core/types.h"
#include "pimio/scan/media_hasher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <algorithm>

namespace pimio::scan {

namespace {

QString pathKey(const QString &path)
{
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

QString managedOriginalPath(const core::MediaId &id, const core::ContentFingerprint &fingerprint,
                            const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const QString extension = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
    const QString encodedId = QString::fromLatin1(
            QCryptographicHash::hash(id.value().toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString encodedContent = QString::fromLatin1(
            QCryptographicHash::hash(fingerprint.digest().toUtf8(), QCryptographicHash::Sha256)
                    .toHex());
    return QStringLiteral("originals/%1/%2/%3%4")
            .arg(encodedId.left(2), encodedId, encodedContent, extension);
}

bool isWithinRoot(const QString &path, const QString &root)
{
    const QString pathValue = pathKey(path);
    QString rootValue = pathKey(root);
    if (pathValue == rootValue) {
        return true;
    }
    if (!rootValue.endsWith(QLatin1Char('/'))) {
        rootValue.append(QLatin1Char('/'));
    }
    return pathValue.startsWith(rootValue);
}

/// Determines MediaKind from a file-name extension.
core::MediaKind kindFromExtension(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    static const QSet<QString> imageExts{
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("tiff"), QStringLiteral("tif"),  QStringLiteral("bmp"),
        QStringLiteral("gif"),  QStringLiteral("webp"), QStringLiteral("heic"),
        QStringLiteral("heif"), QStringLiteral("avif"), QStringLiteral("cr2"),
        QStringLiteral("cr3"),  QStringLiteral("nef"),  QStringLiteral("arw"),
        QStringLiteral("raf"),  QStringLiteral("dng"),  QStringLiteral("orf"),
        QStringLiteral("rw2"),  QStringLiteral("pef"),
    };
    static const QSet<QString> videoExts{
        QStringLiteral("mp4"),  QStringLiteral("mov"),  QStringLiteral("avi"),
        QStringLiteral("mkv"),  QStringLiteral("m4v"),  QStringLiteral("wmv"),
        QStringLiteral("flv"),  QStringLiteral("webm"), QStringLiteral("3gp"),
        QStringLiteral("mts"),  QStringLiteral("m2ts"),
    };
    if (imageExts.contains(ext)) {
        return core::MediaKind::Image;
    }
    if (videoExts.contains(ext)) {
        return core::MediaKind::Video;
    }
    return core::MediaKind::Unknown;
}

/// Recursively collects all file entries under \a dirPath. Directories are
/// traversed but not added to the result. Entries within each directory are
/// sorted by fileName for determinism. Symlinks are skipped when
/// \a followSymlinks is false.
void collectEntries(const QString &dirPath, bool followSymlinks, core::FileSystem &fs,
                    QList<core::DirectoryEntry> &result)
{
    core::Error listError;
    const QList<core::DirectoryEntry> entries = fs.listDirectory(dirPath, &listError);
    if (listError.isError()) {
        // Permission or I/O error listing this directory; skip it.
        return;
    }

    QList<core::DirectoryEntry> sortedEntries = entries;
    std::sort(sortedEntries.begin(), sortedEntries.end(),
              [](const core::DirectoryEntry &a, const core::DirectoryEntry &b) {
                  return a.fileName < b.fileName;
              });

    for (const core::DirectoryEntry &entry : std::as_const(sortedEntries)) {
        if (entry.isSymbolicLink && !followSymlinks) {
            continue;
        }
        if (entry.isDirectory) {
            collectEntries(entry.absolutePath, followSymlinks, fs, result);
        } else {
            result.append(entry);
        }
    }
}

} // namespace

class Scanner::Private
{
public:
    core::FileSystem *fs;
    core::MetadataReader *reader; // nullable
    core::DurableStore *store;
    int commitBatchSize = 0;
};

Scanner::Scanner(core::FileSystem *fs, core::MetadataReader *reader, core::DurableStore *store)
    : d(new Private{fs, reader, store, 0})
{
}

Scanner::~Scanner()
{
    delete d;
}

void Scanner::setCommitBatchSize(int records)
{
    d->commitBatchSize = std::max(0, records);
}

int Scanner::commitBatchSize() const
{
    return d->commitBatchSize;
}

core::Error Scanner::scan(const LibraryRoot &root, const std::atomic<bool> &isCancelled,
                          Result *result, const ProgressCallback &onProgress)
{
    // Hard check: root must exist.
    if (!d->fs->exists(root.absolutePath) || !d->fs->isDirectory(root.absolutePath)) {
        return core::Error(core::ErrorCode::NotFound,
                           QStringLiteral("Library root does not exist: %1")
                               .arg(root.absolutePath));
    }

    Result localResult;
    if (result == nullptr) {
        result = &localResult;
    }

    // ---- Load existing records ----

    core::Error loadError;
    const QList<core::MediaId> existingIds = d->store->listIds(&loadError);
    if (loadError.isError()) {
        return loadError;
    }

    // byPath: normalized absolutePath → records (for all records under this root).
    // A list is intentional: older versions could persist multiple ids for one
    // path, and reconciliation must retain one while removing the extras.
    // byDigest: fingerprint digest → list of records (for move detection).
    QHash<QString, QList<core::MediaRecord>> byPath;
    QHash<QString, QList<core::MediaRecord>> byDigest;
    byPath.reserve(existingIds.size());

    for (const core::MediaId &id : std::as_const(existingIds)) {
        core::Error recError;
        const auto rec = d->store->load(id, &recError);
        if (!rec.has_value()) {
            continue; // concurrent removal or transient error; skip
        }
        const QString path = rec->identity.absolutePath;
        // Only index records that belong to this root.
        if (!isWithinRoot(path, root.absolutePath)) {
            continue;
        }
        byPath[pathKey(path)].append(*rec);
        if (rec->fingerprint.isValid()) {
            byDigest[rec->fingerprint.digest()].append(*rec);
        }
    }

    // ---- Walk the filesystem ----

    QList<core::DirectoryEntry> foundEntries;
    collectEntries(root.absolutePath, root.followSymlinks, *d->fs, foundEntries);

    // Build the set of all found paths for move-detection lookup.
    QSet<QString> foundPaths;
    foundPaths.reserve(foundEntries.size());
    for (const core::DirectoryEntry &entry : std::as_const(foundEntries)) {
        foundPaths.insert(pathKey(entry.absolutePath));
    }

    // ---- Reconcile each found file ----

    QSet<QString> seenIds; // ids of records that still exist on disk
    QSet<QString> failedPaths; // source paths that could not be reconciled safely

    // Records staged since the last commit. Batching them lets a browser show
    // part of a library while the rest is still being scanned: a record is
    // invisible outside this process until it is committed.
    QList<core::MediaRecord> pendingBatch;

    auto commitBatch = [&]() -> core::Error {
        if (!d->store->hasStagedChanges()) {
            pendingBatch.clear();
            return {};
        }
        core::Error commitError;
        const auto checkpoint =
            d->store->commit(QStringLiteral("Scan: %1").arg(root.absolutePath), &commitError);
        if (!checkpoint.has_value()) {
            return commitError;
        }
        if (onProgress && !pendingBatch.isEmpty()) {
            onProgress(pendingBatch, *result);
        }
        pendingBatch.clear();
        return {};
    };

    auto commitBatchIfFull = [&]() -> core::Error {
        if (d->commitBatchSize <= 0 || pendingBatch.size() < d->commitBatchSize) {
            return {};
        }
        return commitBatch();
    };

    for (const core::DirectoryEntry &entry : std::as_const(foundEntries)) {
        if (isCancelled.load()) {
            d->store->discardStaged(nullptr);
            return core::Error::cancelled();
        }

        const QString &path = entry.absolutePath;
        const core::FileIdentity &identity = entry.identity;

        // Skip files that are not media: neither a recognised media extension
        // nor a container the reader can parse. This keeps sidecar leftovers and
        // OS bookkeeping files such as .DS_Store or Thumbs.db out of the library
        // instead of indexing them as broken, thumbnail-less "gray square" items.
        // Files already indexed under this path are left to the removal pass, so
        // a file that stops being media is dropped rather than kept stale.
        const bool isIndexableMedia =
            kindFromExtension(QFileInfo(path).fileName()) != core::MediaKind::Unknown
            || (d->reader != nullptr && d->reader->supports(path));
        if (!isIndexableMedia) {
            continue;
        }

        const QString currentPathKey = pathKey(path);
        if (byPath.contains(currentPathKey)) {
            // File was known at this path.
            core::MediaRecord existing = byPath.value(currentPathKey).constFirst();

            if (identity.looksUnchangedFrom(existing.identity)
                && existing.originalStorage == core::MediaRecord::OriginalStorage::Managed) {
                // Cheap check says nothing changed; skip recomputing fingerprint.
                seenIds.insert(existing.id.value());
                ++result->unchanged;
                continue;
            }

            // Identity changed (size/mtime); recompute fingerprint.
            core::Error hashError;
            const core::ContentFingerprint fp =
                MediaHasher::fingerprintFile(path, *d->fs, &hashError);
            if (hashError.isError()) {
                failedPaths.insert(currentPathKey);
                result->warnings.append(hashError.withContext(
                    QJsonObject{{QStringLiteral("path"), path}}));
                continue;
            }

            existing.identity = identity;
            existing.fingerprint = fp;
            existing.originalStorage = core::MediaRecord::OriginalStorage::Managed;
            const QString previousManagedPath = existing.managedOriginalPath;
            existing.managedOriginalPath =
                    managedOriginalPath(existing.id, fp, QFileInfo(path).fileName());

            // Refresh metadata if a reader is available and supports this file.
            if (d->reader != nullptr && d->reader->supports(path)) {
                core::Error metaError;
                const auto metaResult = d->reader->read(path, &metaError);
                if (metaResult.has_value()) {
                    existing.metadata = metaResult->metadata;
                    // Damage the reader recovered from still has to reach the
                    // user; the item is indexed, but not silently.
                    for (const core::Error &warning : std::as_const(metaResult->warnings)) {
                        result->warnings.append(warning);
                    }
                } else if (metaError.isError()) {
                    result->warnings.append(metaError.withContext(
                        QJsonObject{{QStringLiteral("path"), path}}));
                }
            }
            existing.metadata.fileName = QFileInfo(path).fileName();
            existing.metadata.folderPath = QFileInfo(path).absolutePath();

            core::Error stageError;
            const bool originalChanged = previousManagedPath != existing.managedOriginalPath;
            const bool staged = originalChanged ? d->store->stageOriginal(existing, path,
                                                                           &stageError)
                                                : d->store->stage(existing, &stageError);
            if (!staged) {
                return stageError;
            }
            seenIds.insert(existing.id.value());
            ++result->updated;
            pendingBatch.append(existing);
            if (const core::Error batchError = commitBatchIfFull(); batchError.isError()) {
                return batchError;
            }

        } else {
            // File is new at this path; compute fingerprint.
            core::Error hashError;
            const core::ContentFingerprint fp =
                MediaHasher::fingerprintFile(path, *d->fs, &hashError);
            if (hashError.isError()) {
                failedPaths.insert(currentPathKey);
                result->warnings.append(hashError.withContext(
                    QJsonObject{{QStringLiteral("path"), path}}));
                continue;
            }

            // Move detection: look for an existing record with the same
            // fingerprint whose old path is no longer on disk.
            core::MediaRecord newRecord;
            bool isMoved = false;

            if (fp.isValid()) {
                const QList<core::MediaRecord> &candidates = byDigest.value(fp.digest());
                for (const core::MediaRecord &candidate : candidates) {
                    const QString &oldPath = candidate.identity.absolutePath;
                    if (!foundPaths.contains(pathKey(oldPath))
                        && !seenIds.contains(candidate.id.value())) {
                        // The original path is gone: this is a move/rename.
                        newRecord = candidate;
                        isMoved = true;
                        break;
                    }
                }
            }

            if (!isMoved) {
                newRecord.id = core::MediaId::generate();
                newRecord.metadata.kind = kindFromExtension(QFileInfo(path).fileName());
            }

            const bool alreadyManaged =
                    isMoved
                    && newRecord.originalStorage == core::MediaRecord::OriginalStorage::Managed;
            newRecord.identity = identity;
            newRecord.fingerprint = fp;
            newRecord.originalStorage = core::MediaRecord::OriginalStorage::Managed;
            const QString previousManagedPath = newRecord.managedOriginalPath;
            newRecord.managedOriginalPath =
                    managedOriginalPath(newRecord.id, fp, QFileInfo(path).fileName());
            newRecord.metadata.fileName = QFileInfo(path).fileName();
            newRecord.metadata.folderPath = QFileInfo(path).absolutePath();

            // Read metadata if a reader is available.
            if (d->reader != nullptr && d->reader->supports(path)) {
                core::Error metaError;
                const auto metaResult = d->reader->read(path, &metaError);
                if (metaResult.has_value()) {
                    newRecord.metadata = metaResult->metadata;
                    newRecord.metadata.fileName = QFileInfo(path).fileName();
                    newRecord.metadata.folderPath = QFileInfo(path).absolutePath();
                    for (const core::Error &warning : std::as_const(metaResult->warnings)) {
                        result->warnings.append(warning);
                    }
                } else if (metaError.isError()) {
                    result->warnings.append(metaError.withContext(
                        QJsonObject{{QStringLiteral("path"), path}}));
                }
            }

            core::Error stageError;
            const bool managedBytesUnchanged =
                    alreadyManaged && previousManagedPath == newRecord.managedOriginalPath;
            const bool staged = managedBytesUnchanged
                    ? d->store->stage(newRecord, &stageError)
                    : d->store->stageOriginal(newRecord, path, &stageError);
            if (!staged) {
                return stageError;
            }
            seenIds.insert(newRecord.id.value());
            isMoved ? ++result->updated : ++result->added;
            pendingBatch.append(newRecord);
            if (const core::Error batchError = commitBatchIfFull(); batchError.isError()) {
                return batchError;
            }
        }
    }

    // ---- Remove records no longer on disk ----

    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it) {
        for (const core::MediaRecord &record : it.value()) {
            if (isCancelled.load()) {
                d->store->discardStaged(nullptr);
                return core::Error::cancelled();
            }
            // Import roots are discovery/provenance inputs, not the durable
            // owner of an item. A missing source must never delete either a
            // managed original or a legacy record still awaiting migration.
            // The one removable case is an old duplicate record for a source
            // path that is still present and has already been reconciled.
            if (!seenIds.contains(record.id.value())
                && foundPaths.contains(it.key()) && !failedPaths.contains(it.key())) {
                core::Error removeError;
                if (!d->store->remove(record.id, &removeError)) {
                    return removeError;
                }
                ++result->removed;
            }
        }
    }

    // ---- Commit all staged changes ----

    if (const core::Error commitError = commitBatch(); commitError.isError()) {
        return commitError;
    }

    return {};
}

} // namespace pimio::scan
