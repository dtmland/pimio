#pragma once

#include "pimio/thumbnail/thumbnail_renderer.h"

namespace pimio::thumbnail {

/// Renders a single video frame as a thumbnail using Qt Multimedia.
///
/// Qt Multimedia is a genuine cross-platform decode path: it dispatches to a
/// native backend per platform (Windows Media Foundation, GStreamer on
/// Linux, AVFoundation on macOS), so no platform-specific decoder code lives
/// in pimio itself. Decoding runs on whatever thread calls render(), which is
/// normally one of ThumbnailService's worker threads; QMediaPlayer and
/// QVideoSink are only ever touched from that single thread for the lifetime
/// of the call, and both are destroyed before render() returns.
///
/// render() plays the source from the beginning (or seeks to
/// \c request.positionMs when it is greater than zero) and captures the
/// first video frame it receives at or after that position. The result is
/// encoded as JPEG, matching ImageRenderer's output so the browser model does
/// not need to know which renderer produced a thumbnail.
///
/// A file that cannot be opened or has no playable video stream yields
/// ErrorCode::UnsupportedMedia. A file that starts loading but fails during
/// decode yields ErrorCode::CorruptData. A file that never produces a frame
/// within a bounded wait yields ErrorCode::Timeout rather than hanging the
/// calling thread forever.
class VideoFrameRenderer final : public ThumbnailRenderer
{
public:
    /// \copydoc ThumbnailRenderer::render
    core::MediaResult render(const core::MediaRequest &request,
                             core::Error *error) const override;
};

} // namespace pimio::thumbnail
