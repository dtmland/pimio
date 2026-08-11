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

void ThumbnailImageProvider::removeImage(const QString &mediaId)
{
    QMutexLocker locker(&m_mutex);
    m_images.remove(mediaId);
}

bool ThumbnailImageProvider::contains(const QString &mediaId) const
{
    QMutexLocker locker(&m_mutex);
    return m_images.contains(mediaId);
}

void ThumbnailImageProvider::setCapacity(int images)
{
    QMutexLocker locker(&m_mutex);
    m_images.setMaxCost(qMax(1, images));
}

int ThumbnailImageProvider::capacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_images.maxCost();
}

int ThumbnailImageProvider::count() const
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_images.size());
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
