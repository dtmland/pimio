#pragma once

#include "pimio/core/error.h"
#include "pimio/core/metadata.h"

#include <QString>

#include <optional>

namespace pimio::core {

/// Result of reading metadata for one file.
struct MetadataReadResult
{
    MediaMetadata metadata;

    /// True when an adjacent XMP sidecar contributed to the result.
    bool usedSidecar = false;

    /// Non-fatal problems, such as an unreadable EXIF block in an otherwise
    /// usable file. The item is still indexed.
    QList<Error> warnings;
};

/// Metadata boundary.
///
/// Implementations wrap a concrete library. Keeping the boundary abstract lets
/// the adapter choice be deferred and lets contract tests run without any
/// third-party dependency.
class MetadataReader
{
public:
    virtual ~MetadataReader();

    /// True when this reader claims support for the file, based on content
    /// rather than extension alone where practical.
    virtual bool supports(const QString &absolutePath) const = 0;

    /// Reads metadata. Returns nothing and sets \a error when the file cannot
    /// be read at all. Unsupported media must produce ErrorCode::UnsupportedMedia
    /// rather than a crash, so the scan can record it and continue.
    virtual std::optional<MetadataReadResult> read(const QString &absolutePath,
                                                   Error *error) const = 0;
};

/// Portable metadata write boundary.
///
/// Writes must never destroy the previous valid file. Implementations are
/// expected to write atomically and to report a conflict rather than
/// overwriting a change made outside pimio.
class MetadataWriter
{
public:
    virtual ~MetadataWriter();

    /// True when the target can accept embedded writes. RAW files normally
    /// cannot, and must use a sidecar instead.
    virtual bool supportsEmbeddedWrite(const QString &absolutePath) const = 0;

    /// Path of the sidecar pimio would use for \a absolutePath.
    virtual QString sidecarPathFor(const QString &absolutePath) const = 0;

    /// Applies \a metadata. \a expectedOrigin records what pimio believed the
    /// current state was; a mismatch must produce ErrorCode::Conflict.
    virtual bool write(const QString &absolutePath, const MediaMetadata &metadata,
                       MetadataOrigin expectedOrigin, Error *error) = 0;
};

} // namespace pimio::core
