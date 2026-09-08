#pragma once

#include "pimio/core/metadata_reader.h"

#include <QByteArray>

namespace pimio::metadata {

/// Portable XMP writer that leaves media bytes untouched.
///
/// It writes a separate sidecar, keeps all non-pimio XML byte-for-byte, and
/// compares a snapshot taken while editing before replacing the sidecar.
class XmpSidecarWriter final : public core::MetadataWriter
{
public:
    struct Snapshot {
        QString path;
        QByteArray bytes;
        bool existed = false;
    };

    bool supportsEmbeddedWrite(const QString &absolutePath) const override;
    QString sidecarPathFor(const QString &absolutePath) const override;
    bool write(const QString &absolutePath, const core::MediaMetadata &metadata,
               core::MetadataOrigin expectedOrigin, core::Error *error) override;

    Snapshot snapshot(const QString &absolutePath, core::Error *error) const;
    bool write(const Snapshot &expected, const core::MediaMetadata &metadata,
               const core::EditRecipe &recipe, core::Error *error) const;
};

} // namespace pimio::metadata
