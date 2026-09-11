#pragma once

#include "pimio/core/metadata_reader.h"

#include <QString>
#include <QStringList>

namespace pimio::metadata {

/// Embedded metadata writer backed by the pinned ExifTool distribution.
///
/// ExifTool runs as a separate process so its Artistic/GPL dual licence does
/// not change the linking terms of the application. Callers pass a private
/// working copy; the durable store publishes that copy atomically.
class ExifToolMetadataWriter final : public core::MetadataWriter
{
public:
    ExifToolMetadataWriter();
    ExifToolMetadataWriter(QString program, QStringList prefixArguments);

    bool isAvailable() const;
    bool supportsEmbeddedWrite(const QString &absolutePath) const override;
    bool write(const QString &absolutePath, const core::MediaMetadata &metadata,
               const core::ContentFingerprint &expectedFingerprint,
               core::Error *error) override;

private:
    QString m_program;
    QStringList m_prefixArguments;
};

} // namespace pimio::metadata
