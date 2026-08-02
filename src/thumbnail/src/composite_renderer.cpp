#include "pimio/thumbnail/composite_renderer.h"

#include "pimio/core/error.h"

namespace pimio::thumbnail {

CompositeRenderer::CompositeRenderer(ThumbnailRenderer *imageRenderer,
                                     ThumbnailRenderer *videoRenderer)
    : m_imageRenderer(imageRenderer)
    , m_videoRenderer(videoRenderer)
{
}

core::MediaResult CompositeRenderer::render(const core::MediaRequest &request,
                                            core::Error *error) const
{
    core::Error imageError;
    if (m_imageRenderer != nullptr) {
        const core::MediaResult result = m_imageRenderer->render(request, &imageError);
        if (!imageError.isError()) {
            return result;
        }
        if (imageError.code() != core::ErrorCode::UnsupportedMedia) {
            // A recognised-but-broken image is still an image; do not mask
            // that diagnosis by falling through to the video decoder.
            if (error) {
                *error = imageError;
            }
            return {};
        }
    }

    if (m_videoRenderer != nullptr) {
        core::Error videoError;
        const core::MediaResult result = m_videoRenderer->render(request, &videoError);
        if (!videoError.isError()) {
            return result;
        }
        if (error) {
            *error = videoError;
        }
        return {};
    }

    if (error) {
        *error = imageError.isError()
                ? imageError
                : core::Error(core::ErrorCode::UnsupportedMedia,
                              QStringLiteral("No renderer available for: %1")
                                      .arg(request.absolutePath));
    }
    return {};
}

} // namespace pimio::thumbnail
