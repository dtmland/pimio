#include "pimio/editing/metadata_edit_service.h"

#include <QCryptographicHash>
#include <QFile>

namespace pimio::editing {
namespace {

void assignError(core::Error *error, core::ErrorCode code, const QString &message)
{
    if (error) {
        *error = core::Error(code, message);
    }
}

QString sha256(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

void restoreAfterFailedSave(core::DurableStore &store, core::Error *error)
{
    const QString saveFailure = error ? error->message() : QString();
    core::Error restoreError;
    const bool restored = store.restoreFromDurableState(&restoreError);
    const bool discarded = store.discardStaged(&restoreError);
    if ((!restored || !discarded) && error) {
        *error = core::Error(
                core::ErrorCode::StorageUnavailable,
                QStringLiteral("%1 The committed Library checkout could not be restored: %2")
                        .arg(saveFailure, restoreError.message()));
    }
}

} // namespace

MetadataEditService::MetadataEditService(core::DurableStore &store,
                                         core::MetadataWriter &writer)
    : m_store(store)
    , m_writer(writer)
{
}

bool MetadataEditService::stage(const core::MediaId &id, core::MediaMetadata metadata,
                                core::EditRecipe recipe, core::Error *error)
{
    const auto existing = m_edits.constFind(id.value());
    core::MediaRecord original;
    if (existing != m_edits.constEnd()) {
        original = existing->original;
    } else {
        const auto loaded = m_store.load(id, error);
        if (!loaded) {
            return false;
        }
        original = *loaded;
    }
    if (original.originalStorage != core::MediaRecord::OriginalStorage::Managed) {
        assignError(error, core::ErrorCode::UnsupportedMedia,
                    QStringLiteral("Metadata can only be saved to managed originals."));
        return false;
    }

    metadata.normalize();
    if (recipe != original.recipe) {
        recipe.setRevision(original.recipe.revision() + 1);
    }
    core::MediaRecord edited = original;
    edited.metadata = std::move(metadata);
    edited.recipe = std::move(recipe);
    m_edits.insert(id.value(), Edit{original, std::move(edited)});
    return true;
}

std::optional<core::MediaRecord>
MetadataEditService::staged(const core::MediaId &id) const
{
    const auto edit = m_edits.constFind(id.value());
    return edit == m_edits.constEnd() ? std::nullopt
                                     : std::optional<core::MediaRecord>(edit->edited);
}

bool MetadataEditService::hasStagedEdits() const
{
    return !m_edits.isEmpty();
}

bool MetadataEditService::cancel(core::Error *error)
{
    if (m_store.hasStagedChanges() && !m_store.discardStaged(error)) {
        return false;
    }
    m_edits.clear();
    return true;
}

std::optional<core::Checkpoint> MetadataEditService::save(const QString &message,
                                                          core::Error *error)
{
    if (m_edits.isEmpty()) {
        assignError(error, core::ErrorCode::Conflict,
                    QStringLiteral("There are no edits to save."));
        return std::nullopt;
    }
    if (m_store.hasStagedChanges()) {
        assignError(error, core::ErrorCode::Conflict,
                    QStringLiteral("Other Library changes must finish before Save."));
        return std::nullopt;
    }

    QList<core::MetadataWriteRequest> writes;
    QHash<QString, QString> editedPaths;
    for (auto edit = m_edits.begin(); edit != m_edits.end(); ++edit) {
        const auto current = m_store.load(edit->original.id, error);
        if (!current || *current != edit->original) {
            assignError(error, core::ErrorCode::Conflict,
                        QStringLiteral("The Library item changed after editing began."));
            m_store.discardStaged(nullptr);
            return std::nullopt;
        }
        const QString sourcePath = m_store.originalPath(edit->original, error);
        if (sourcePath.isEmpty()) {
            m_store.discardStaged(nullptr);
            return std::nullopt;
        }
        if (sha256(sourcePath) != edit->original.fingerprint.digest()) {
            assignError(error, core::ErrorCode::Conflict,
                        QStringLiteral("The managed original changed after editing began."));
            m_store.discardStaged(nullptr);
            return std::nullopt;
        }

        if (edit->edited.metadata == edit->original.metadata) {
            if (!m_store.stage(edit->edited, error)) {
                m_store.discardStaged(nullptr);
                return std::nullopt;
            }
            continue;
        }
        if (!m_writer.supportsEmbeddedWrite(sourcePath)) {
            assignError(error, core::ErrorCode::UnsupportedMedia,
                        QStringLiteral("This original does not support embedded metadata."));
            m_store.discardStaged(nullptr);
            return std::nullopt;
        }

        editedPaths.insert(edit.key(), sourcePath);
        writes.append({sourcePath, edit->edited.metadata, edit->original.fingerprint});
    }
    if (!m_writer.writeBatch(writes, error)) {
        restoreAfterFailedSave(m_store, error);
        return std::nullopt;
    }
    for (auto edit = m_edits.begin(); edit != m_edits.end(); ++edit) {
        const QString editedPath = editedPaths.value(edit.key());
        if (editedPath.isEmpty()) {
            continue;
        }
        const QString digest = sha256(editedPath);
        if (digest.isEmpty()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QStringLiteral("Could not fingerprint the updated original."));
            restoreAfterFailedSave(m_store, error);
            return std::nullopt;
        }
        edit->edited.fingerprint =
                core::ContentFingerprint(QStringLiteral("sha256"), digest);
        if (!m_store.stage(edit->edited, error)) {
            restoreAfterFailedSave(m_store, error);
            return std::nullopt;
        }
    }

    const auto checkpoint = m_store.commit(message, error);
    if (checkpoint) {
        m_edits.clear();
    } else {
        restoreAfterFailedSave(m_store, error);
    }
    return checkpoint;
}

} // namespace pimio::editing
