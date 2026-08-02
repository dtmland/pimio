#pragma once

#include "pimio/thumbnail/thumbnail_renderer.h"

#include <memory>

namespace pimio::thumbnail {

/// Dispatches a render request to an image or video renderer.
///
/// The model and service never need to know which concrete renderer produced
/// a thumbnail. CompositeRenderer tries the image renderer first, since Qt's
/// image loader recognises its format from content rather than the file
/// extension and returns ErrorCode::UnsupportedMedia quickly when the file
/// is not an image it understands; the video renderer is then tried as the
/// fallback. If both fail, the image renderer's error is what a still-image
/// consumer would expect to see, so it is the one returned to the caller.
class CompositeRenderer final : public ThumbnailRenderer
{
public:
    /// Constructs a renderer that owns neither \a imageRenderer nor
    /// \a videoRenderer; both must outlive this object. Either may be null,
    /// in which case that media kind is simply unsupported.
    CompositeRenderer(ThumbnailRenderer *imageRenderer, ThumbnailRenderer *videoRenderer);

    core::MediaResult render(const core::MediaRequest &request,
                             core::Error *error) const override;

private:
    ThumbnailRenderer *m_imageRenderer;
    ThumbnailRenderer *m_videoRenderer;
};

} // namespace pimio::thumbnail
