#pragma once

#include "pimio/core/error.h"
#include "pimio/core/file_system.h"
#include "pimio/core/metadata.h"
#include "pimio/core/metadata_reader.h"

#include <QString>

#include <memory>
#include <optional>

namespace pimio::metadata {

/// Reads metadata for the media formats pimio v1 indexes, using no third-party
/// library.
///
/// The formats that carry the metadata this increment needs — JPEG/TIFF EXIF,
/// XMP sidecars, and ISO base media (MP4/MOV) headers — are container formats.
/// Reading their headers requires no decoder, so the read path costs one small
/// parser per container instead of a GPL-licensed dependency that would have to
/// be built and shipped on three platforms. The rationale, and what would
/// change the decision, is recorded in
/// docs/decisions/0002-metadata-adapter.md.
///
/// Nothing here decodes pixels or samples. Decode-dependent work (thumbnails,
/// RAW previews, video frames) belongs to later increments and may still bring
/// in a decoding library.
///
/// ### Precedence
///
/// Values are resolved highest-source-first:
///
///   UserEdit > Sidecar > Embedded > FileSystem
///
/// A sidecar is written deliberately by a person or their editing tool, so it
/// outranks what the camera wrote. Losing the camera's value is not acceptable,
/// so every disagreement is recorded in MediaMetadata::conflicts with both
/// values and both origins, and stays visible to the user.
///
/// ### Failure policy
///
/// A file whose bytes cannot be interpreted at all produces
/// ErrorCode::UnsupportedMedia, so the scan records it and continues. A file
/// that is usable but whose metadata is damaged is still indexed, and the
/// damage is reported through MetadataReadResult::warnings.
class BuiltinMetadataReader final : public core::MetadataReader
{
public:
    /// Constructs a reader.
    ///
    /// \a fileSystem is the boundary used to read bytes. When it is null the
    /// reader reads the local disk directly, which is what an application does;
    /// injecting a filesystem lets a test drive the reader without a disk.
    explicit BuiltinMetadataReader(core::FileSystem *fileSystem = nullptr);
    ~BuiltinMetadataReader() override;

    BuiltinMetadataReader(const BuiltinMetadataReader &) = delete;
    BuiltinMetadataReader &operator=(const BuiltinMetadataReader &) = delete;

    /// True when this reader claims the file.
    ///
    /// It claims anything whose bytes it recognizes, and also anything carrying
    /// a media extension whose bytes it does not recognize. The second case is
    /// deliberate: a `.jpg` holding something else must become a visible
    /// unsupported-media record from read(), not silently disappear from the
    /// scan because nothing claimed it.
    bool supports(const QString &absolutePath) const override;

    std::optional<core::MetadataReadResult> read(const QString &absolutePath,
                                                 core::Error *error) const override;

    /// Path of the XMP sidecar this reader consults for \a absolutePath.
    ///
    /// Both conventions are accepted on read: `photo.jpg.xmp` and `photo.xmp`.
    /// This returns the first that exists, and the `photo.xmp` form when
    /// neither does.
    QString sidecarPathFor(const QString &absolutePath) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::metadata
