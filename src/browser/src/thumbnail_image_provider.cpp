#include "pimio/browser/thumbnail_image_provider.h"

#include <QMutexLocker>

namespace pimio::browser {

ThumbnailImageProvider::ThumbnailImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void ThumbnailImageProvider::setImage(const QString &mediaId, const QImage &image)
{
    QMutexLocker locker(&m_mutex);
    m_images.insert(mediaId, new QImage(image));
}

void ThumbnailImageProvider::clear()
{
    QMutexLocker locker(&m_mutex);
    m_images.clear();
}

QImage ThumbnailImageProvider::requestImage(const QString &id, QSize *size,
                                            const QSize &requestedSize)
{
    // Defensive: every caller in this codebase builds the URL as
    // "image://thumbnail/" + mediaId, but tolerate a leading slash in case a
    // future Qt version normalizes the id differently.
    const QString key = id.startsWith(QLatin1Char('/')) ? id.mid(1) : id;

    QImage image;
    {
        QMutexLocker locker(&m_mutex);
        if (const QImage *cached = m_images.object(key)) {
            image = *cached;
        }
    }

    if (size) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.isValid()) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

} // namespace pimio::browser
