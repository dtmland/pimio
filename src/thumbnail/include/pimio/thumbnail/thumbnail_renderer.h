#pragma once

#include "pimio/core/error.h"
#include "pimio/core/media_request.h"

namespace pimio::thumbnail {

/// Strategy interface for turning a MediaRequest into rendered bytes.
///
/// Implementations are free to use Qt Gui, FFmpeg, or any other tool.
/// Keeping the interface here lets ThumbnailService be unit-tested with a
/// fake renderer that injects controlled results without touching the
/// filesystem.
///
/// All methods must be safe to call from multiple threads simultaneously.
class ThumbnailRenderer
{
public:
    virtual ~ThumbnailRenderer();

    /// Renders \a request into bytes.
    ///
    /// \a request.absolutePath is the source file. The returned MediaResult
    /// contains the encoded bytes, the actual pixel size, and the format
    /// string (for example \c "jpeg"). On failure the method returns a
    /// default-constructed MediaResult and sets \a *error.
    virtual core::MediaResult render(const core::MediaRequest &request,
                                     core::Error *error) const = 0;
};

} // namespace pimio::thumbnail
