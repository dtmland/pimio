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
/// simply looks the id back up.
///
/// The provider retains a bounded number of images so a full-library scroll
/// cannot grow memory without bound. The bound is a backstop, not the policy:
/// the model owns the retention window and removes an image here as soon as
/// it stops claiming that row is Ready, so that a row the grid believes has a
/// thumbnail always has one to serve. A model that lets this cache overflow
/// would leave rows permanently grey, because `image://thumbnail/<id>` would
/// fail for a row the model never re-requests.
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

    /// Forgets the image recorded for \a mediaId, if any. Called by the model
    /// when it drops a row's thumbnail, so the two never disagree about what
    /// can be served.
    void removeImage(const QString &mediaId);

    /// True when an image is currently recorded for \a mediaId.
    bool contains(const QString &mediaId) const;

    /// Maximum number of images retained. Values below 1 are clamped to 1.
    ///
    /// Set by the model to its own retention limit plus a little slack, so
    /// this cache never evicts an image the model still believes is Ready.
    void setCapacity(int images);
    int capacity() const;

    /// Number of images currently recorded.
    int count() const;

    /// Forgets every recorded thumbnail, for example when the model reloads
    /// against a different library.
    void clear();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    mutable QMutex m_mutex;
    QCache<QString, QImage> m_images{512};
};

} // namespace pimio::browser
