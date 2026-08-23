#pragma once

#include "pimio/thumbnail/thumbnail_renderer.h"

namespace pimio::thumbnail {

/// Renders image thumbnails using Qt's built-in image loading.
///
/// Supports every format Qt can decode through QImageReader. WebP is provided
/// by Qt ImageFormats; AVIF and HEIC are provided by pimio's bundled image
/// plugins. Video frames and formats Qt cannot recognize are not handled here;
/// a separate video renderer must be registered for those.
///
/// The rendered result is encoded as JPEG at 85 % quality. The image is
/// scaled to fit within request.targetSize preserving the original aspect
/// ratio (KeepAspectRatio, SmoothTransformation).
class ImageRenderer final : public ThumbnailRenderer
{
public:
    /// \copydoc ThumbnailRenderer::render
    ///
    /// Returns ErrorCode::UnsupportedMedia when Qt's loader cannot identify
    /// the format, and ErrorCode::CorruptData when the format is recognized
    /// but the file cannot be decoded.
    core::MediaResult render(const core::MediaRequest &request,
                             core::Error *error) const override;
};

} // namespace pimio::thumbnail
