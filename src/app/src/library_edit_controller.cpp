#include "library_edit_controller.h"

namespace pimio::app {

bool LibraryEditController::hasStagedEdits() const
{
    return !m_pendingEdits.isEmpty();
}

LibraryEditController::PendingEdit *LibraryEditController::pendingEditFor(
        core::DurableStore &store, const QString &mediaId, core::Error *error)
{
    const auto existing = m_pendingEdits.find(mediaId);
    if (existing != m_pendingEdits.end()) {
        return &*existing;
    }
    const auto record = store.load(core::MediaId(mediaId), error);
    if (!record) {
        return nullptr;
    }
    core::Error pathError;
    const QString originalPath = store.originalPath(*record, &pathError);
    if (pathError.isError()) {
        if (error) {
            *error = pathError;
        }
        return nullptr;
    }
    const auto snapshot = m_metadataWriter.snapshot(originalPath, error);
    if (error && error->isError()) {
        return nullptr;
    }
    return &*m_pendingEdits.insert(mediaId, {*record, snapshot});
}

bool LibraryEditController::stageMetadata(core::DurableStore &store, const QString &mediaId,
                                          const QString &caption, int rating, const QString &tags,
                                          core::Error *error)
{
    PendingEdit *pending = pendingEditFor(store, mediaId, error);
    if (!pending) {
        return false;
    }
    pending->record.metadata.caption = caption;
    pending->record.metadata.rating = rating;
    pending->record.metadata.tags = tags.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &tag : pending->record.metadata.tags) {
        tag = tag.trimmed();
    }
    pending->record.metadata.tags.removeAll(QString());
    pending->record.metadata.normalize();
    pending->record.metadata.captureTimeOrigin = core::MetadataOrigin::UserEdit;
    return true;
}

bool LibraryEditController::rotate(core::DurableStore &store, const QString &mediaId, int degrees,
                                   core::Error *error)
{
    if (degrees % 90 != 0) {
        if (error) {
            *error = core::Error(core::ErrorCode::Conflict,
                                 QStringLiteral("Rotation must be a multiple of 90 degrees."));
        }
        return false;
    }
    PendingEdit *pending = pendingEditFor(store, mediaId, error);
    if (!pending) {
        return false;
    }
    pending->record.recipe.append(core::EditOperation(
            core::EditOperationKind::Rotate, {{QStringLiteral("degrees"), degrees}}));
    pending->record.recipe.setRevision(pending->record.recipe.revision() + 1);
    return true;
}

bool LibraryEditController::orient(core::DurableStore &store, const QString &mediaId, int value,
                                   core::Error *error)
{
    if (value < 1 || value > 8) {
        if (error) {
            *error = core::Error(core::ErrorCode::Conflict,
                                 QStringLiteral("Orientation must be an EXIF value from 1 to 8."));
        }
        return false;
    }
    PendingEdit *pending = pendingEditFor(store, mediaId, error);
    if (!pending) {
        return false;
    }
    pending->record.recipe.append(core::EditOperation(
            core::EditOperationKind::Orientation, {{QStringLiteral("value"), value}}));
    pending->record.recipe.setRevision(pending->record.recipe.revision() + 1);
    return true;
}

bool LibraryEditController::crop(core::DurableStore &store, const QString &mediaId, int x, int y,
                                 int width, int height, core::Error *error)
{
    if (width <= 0 || height <= 0) {
        if (error) {
            *error = core::Error(core::ErrorCode::Conflict,
                                 QStringLiteral("Crop width and height must be positive."));
        }
        return false;
    }
    PendingEdit *pending = pendingEditFor(store, mediaId, error);
    if (!pending) {
        return false;
    }
    pending->record.recipe.append(core::EditOperation(
            core::EditOperationKind::Crop, {{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
                                            {QStringLiteral("width"), width},
                                            {QStringLiteral("height"), height}}));
    pending->record.recipe.setRevision(pending->record.recipe.revision() + 1);
    return true;
}

std::optional<core::Checkpoint> LibraryEditController::save(core::DurableStore &store,
                                                             core::Error *error)
{
    if (m_pendingEdits.isEmpty()) {
        if (error) {
            *error = core::Error(core::ErrorCode::Conflict, QStringLiteral("There are no staged edits."));
        }
        return std::nullopt;
    }
    for (auto it = m_pendingEdits.begin(); it != m_pendingEdits.end(); ++it) {
        core::Error writeError;
        const QString originalPath = store.originalPath(it->record, &writeError);
        if (writeError.isError() || !m_metadataWriter.write(it->sidecar, it->record.metadata,
                                                            it->record.recipe, &writeError)) {
            if (error) {
                *error = writeError;
            }
            return std::nullopt;
        }
        // A failed durable checkpoint retains staged work. Advance the expected
        // snapshot so retrying recognizes this safe sidecar write as ours.
        it->sidecar = m_metadataWriter.snapshot(originalPath, &writeError);
        if (writeError.isError() || !store.stage(it->record, &writeError)) {
            if (error) {
                *error = writeError;
            }
            return std::nullopt;
        }
    }
    const auto checkpoint = store.commit(QStringLiteral("Save metadata and image recipes"), error);
    if (checkpoint) {
        m_pendingEdits.clear();
    }
    return checkpoint;
}

void LibraryEditController::discard()
{
    m_pendingEdits.clear();
}

std::optional<core::MediaRecord> LibraryEditController::exportImage(
        core::DurableStore &store, const QString &mediaId, const QString &destination,
        core::Error *error)
{
    const auto source = store.load(core::MediaId(mediaId), error);
    if (!source) {
        return std::nullopt;
    }
    const QString sourcePath = store.originalPath(*source, error);
    if (error && error->isError()) {
        return std::nullopt;
    }
    return m_exportService.exportImage(*source, sourcePath, destination, error);
}

} // namespace pimio::app
