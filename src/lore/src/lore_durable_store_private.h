#pragma once

// Internal header shared by lore_durable_store.cpp and
// lore_durable_store_commit.cpp. Provides the Operation event collector,
// failure-mapping helpers, and the Private class definition.

#include "pimio/lore/lore_durable_store.h"

#include "lore_api.h"
#include "lore_store_helpers.h"

#include <QDir>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>

#include <cstring>
#include <memory>

namespace pimio::lore {

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

    void applyMetadata(const lore_metadata_event_data_t &metadata)
    {
        if (checkpoints.isEmpty()) {
            return;
        }
        core::Checkpoint &checkpoint = checkpoints.last();
        const QString key = fromLoreString(metadata.key);
        if (key == QLatin1String("message")
            && metadata.value.tag == LORE_METADATA_TYPE_STRING) {
            checkpoint.message = fromLoreString(metadata.value.string);
        } else if (key == QLatin1String("timestamp")
                   && metadata.value.tag == LORE_METADATA_TYPE_NUMERIC) {
            checkpoint.createdAtUtc = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(metadata.value.numeric), Qt::UTC);
        }
    }

    QMutex m_mutex;
};

/// Maps a LORE failure onto the stable pimio error vocabulary.
inline core::ErrorCode mapFailure(const Operation &operation)
{
    const QString msg = operation.message.toLower();
    if (msg.contains(QLatin1String("nothing staged"))) {
        return core::ErrorCode::Conflict;
    }
    if (msg.contains(QLatin1String("permission"))
        || msg.contains(QLatin1String("access is denied"))
        || msg.contains(QLatin1String("read-only"))) {
        return core::ErrorCode::PermissionDenied;
    }
    if (msg.contains(QLatin1String("no space"))
        || msg.contains(QLatin1String("disk full"))) {
        return core::ErrorCode::OutOfSpace;
    }
    return core::ErrorCode::Internal;
}

inline QJsonObject failureContext(const Operation &operation, const QString &call)
{
    QJsonObject context;
    context.insert(QStringLiteral("loreCall"), call);
    context.insert(QStringLiteral("loreStatus"), operation.status);
    return context;
}

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
    QString libraryDescriptorPath() const
    {
        return recordsPath() + QStringLiteral("/.pimio-library.json");
    }
    QString stagedLibraryDescriptorPath() const
    {
        return stagingPath() + QStringLiteral("/.pimio-library.json");
    }
    QString lorePath() const { return repositoryPath() + QStringLiteral("/.lore"); }

    QString committedRecordPath(const core::MediaId &id) const
    {
        const QString fileName = detail::recordFileName(id);
        return recordsPath() + QLatin1Char('/') + detail::shardFor(fileName)
               + QLatin1Char('/') + fileName;
    }

    QString stagedRecordPath(const core::MediaId &id) const
    {
        const QString fileName = detail::recordFileName(id);
        return stagingPath() + QLatin1Char('/') + detail::shardFor(fileName)
               + QLatin1Char('/') + fileName;
    }

    QString stagedTombstonePath(const core::MediaId &id) const
    {
        const QString baseName = detail::recordFileName(id);
        const QString fileName =
            baseName.left(baseName.size() - 5) + QStringLiteral(".tombstone");
        return stagingPath() + QLatin1Char('/') + detail::shardFor(baseName)
               + QLatin1Char('/') + fileName;
    }

    lore_global_args_t globals() const
    {
        lore_global_args_t args;
        std::memset(&args, 0, sizeof(args));
        args.repository_path = loreString(repositoryPathUtf8);
        args.offline = 1;
        args.local = 1;
        args.sync_data = 1;
        return args;
    }

    bool available() const { return opened && LoreApi::instance().isLoaded(); }

    bool restoreCheckoutToCommittedState(core::Error *error);

    QString queryStateToken(core::Error *error) const;
    bool runStatus(Operation &operation, bool checkDirty, core::Error *error) const;

    QString storePath;
    QByteArray repositoryPathUtf8;
    bool opened = false;
    bool repairedOnOpen = false;
    std::unique_ptr<QLockFile> writerLock;
};

} // namespace pimio::lore
