#pragma once

#include "pimio/core/durable_store.h"
#include "pimio/core/metadata_reader.h"

#include <QHash>

namespace pimio::editing {

/// Holds user edits in memory until an explicit Save publishes one checkpoint.
class MetadataEditService
{
public:
    MetadataEditService(core::DurableStore &store, core::MetadataWriter &writer);

    bool stage(const core::MediaId &id, core::MediaMetadata metadata,
               core::EditRecipe recipe, core::Error *error);
    std::optional<core::MediaRecord> staged(const core::MediaId &id) const;
    bool hasStagedEdits() const;

    bool cancel(core::Error *error);
    std::optional<core::Checkpoint> save(const QString &message, core::Error *error);

private:
    struct Edit
    {
        core::MediaRecord original;
        core::MediaRecord edited;
    };

    core::DurableStore &m_store;
    core::MetadataWriter &m_writer;
    QHash<QString, Edit> m_edits;
};

} // namespace pimio::editing
