#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

namespace pimio::browser {

/// Serves the `image://thumbnail/<mediaId>` URLs used by the QML grid and
/// detail view.
///
/// The provider itself holds no reference to MediaLibraryModel or to any
/// filesystem path: MediaLibraryModel pushes each decoded thumbnail into it
/// as soon as a request completes (see setImage()), and requestImage()
/// simply looks the id back up. The provider retains at most 512 recently used
/// images, preventing a full-library scroll from growing memory without bound.
/// This keeps the provider safe to call from
/// whatever thread Qt Quick chooses for an asynchronous Image element
/// (DetailView.qml sets `asynchronous: true`, which makes Qt Quick call
/// image providers from a thread pool rather than the GUI thread) without
/// the provider needing to touch the model, the projection database, or the
/// thumbnail cache directly.
class ThumbnailImageProvider final : public QQuickImageProvider
{
public:
    ThumbnailImageProvider();

    /// Records the decoded thumbnail for \a mediaId so a subsequent
    /// `image://thumbnail/<mediaId>` request can serve it. Safe to call from
    /// any thread.
    void setImage(const QString &mediaId, const QImage &image);

    /// Forgets every recorded thumbnail, for example when the model reloads
    /// against a different library.
    void clear();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    mutable QMutex m_mutex;
    QCache<QString, QImage> m_images{512};
};

} // namespace pimio::browser
