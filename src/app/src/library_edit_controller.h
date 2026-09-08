#pragma once

#include "pimio/editing/image_export_service.h"
#include "pimio/metadata/xmp_sidecar_writer.h"

#include <QHash>

namespace pimio::app {

class LibraryEditController
{
public:
    bool hasStagedEdits() const;
    bool stageMetadata(core::DurableStore &store, const QString &mediaId, const QString &caption,
                       int rating, const QString &tags, core::Error *error);
    bool rotate(core::DurableStore &store, const QString &mediaId, int degrees, core::Error *error);
    bool orient(core::DurableStore &store, const QString &mediaId, int value, core::Error *error);
    bool crop(core::DurableStore &store, const QString &mediaId, int x, int y, int width, int height,
              core::Error *error);
    std::optional<core::Checkpoint> save(core::DurableStore &store, core::Error *error);
    void discard();
    std::optional<core::MediaRecord> exportImage(core::DurableStore &store, const QString &mediaId,
                                                  const QString &destination, core::Error *error);

private:
    struct PendingEdit {
        core::MediaRecord record;
        metadata::XmpSidecarWriter::Snapshot sidecar;
    };

    PendingEdit *pendingEditFor(core::DurableStore &store, const QString &mediaId,
                                core::Error *error);
    metadata::XmpSidecarWriter m_metadataWriter;
    editing::ImageExportService m_exportService;
    QHash<QString, PendingEdit> m_pendingEdits;
};

} // namespace pimio::app
