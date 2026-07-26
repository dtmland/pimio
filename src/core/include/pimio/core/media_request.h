#pragma once

#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/core/types.h"

#include <QByteArray>
#include <QSize>
#include <QString>

#include <functional>

namespace pimio::core {

/// What kind of rendered bytes the caller wants.
enum class MediaRequestKind {
    Thumbnail,
    Preview,
    VideoFrame,
};

QString toString(MediaRequestKind kind);

/// A request for rendered bytes for one media item.
///
/// The request deliberately says nothing about QML, scene graphs, or textures
/// so the 2.0.0 renderer can consume the same service.
struct MediaRequest
{
    MediaId mediaId;
    ContentFingerprint fingerprint;
    MediaRequestKind kind = MediaRequestKind::Thumbnail;

    /// Absolute filesystem path to the source file. Required by renderers
    /// that read the file directly. Not part of the cache key: identical
    /// content at different paths shares the cache entry.
    QString absolutePath;

    /// Maximum size in device-independent pixels. The result may be smaller.
    QSize targetSize;

    /// Position within a video, in milliseconds. Ignored for images.
    qint64 positionMs = 0;

    /// Revision of the edit recipe the caller expects to see applied. Results
    /// produced for a different revision are stale and must not be shown.
    int recipeRevision = 0;

    JobPriority priority = JobPriority::Interactive;

    /// Deterministic cache key. Two requests that must produce identical bytes
    /// have identical keys, and any field that changes the output changes the
    /// key.
    QString cacheKey() const;
};

/// Rendered bytes plus what they actually represent.
struct MediaResult
{
    QByteArray bytes;

    /// Encoded image format, for example "jpeg" or "png".
    QString format;

    QSize actualSize;

    /// True when this is a lower-quality intermediate and a better result is
    /// still coming. Lets a viewer show progressive previews.
    bool isProgressiveIntermediate = false;
};

/// Handle used to cancel a pending request.
class MediaRequestHandle
{
public:
    MediaRequestHandle() = default;
    explicit MediaRequestHandle(quint64 value);

    bool isValid() const;
    quint64 value() const;

    bool operator==(const MediaRequestHandle &other) const = default;

private:
    quint64 m_value = 0;
};

/// Media-request boundary.
///
/// Callers submit requests and cancel them when a view scrolls away. The
/// service is responsible for bounded concurrency, caching, and dropping stale
/// work.
class MediaRequestService
{
public:
    using ResultCallback = std::function<void(const MediaRequest &, const MediaResult &)>;
    using ErrorCallback = std::function<void(const MediaRequest &, const Error &)>;

    virtual ~MediaRequestService();

    virtual MediaRequestHandle request(const MediaRequest &request, ResultCallback onResult,
                                       ErrorCallback onError) = 0;

    /// Cancels a pending request. Cancelling an already-finished or unknown
    /// handle is not an error.
    virtual void cancel(MediaRequestHandle handle) = 0;

    /// Cancels every request not in \a keep. Views call this after a scroll so
    /// that off-screen work stops immediately.
    virtual void cancelAllExcept(const QList<MediaRequestHandle> &keep) = 0;
};

} // namespace pimio::core
