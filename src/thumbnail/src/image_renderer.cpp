#include "pimio/thumbnail/image_renderer.h"

#include "pimio/core/error.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

namespace pimio::thumbnail {

core::MediaResult ImageRenderer::render(const core::MediaRequest &request,
                                        core::Error *error) const
{
    if (request.absolutePath.isEmpty()) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("MediaRequest.absolutePath is empty"));
        }
        return {};
    }

    // Check whether Qt recognizes the format before attempting a full decode.
    // An empty format string means Qt has no loader for this file.
    const QByteArray format = QImageReader::imageFormat(request.absolutePath);
    if (format.isEmpty()) {
        if (error) {
            *error = core::Error(core::ErrorCode::UnsupportedMedia,
                                 QStringLiteral("Unrecognized image format: %1")
                                         .arg(request.absolutePath));
        }
        return {};
    }

    QImageReader reader(request.absolutePath);
    reader.setAutoTransform(true);  // honour EXIF orientation

    // Scale down while reading when the codec supports it, to avoid loading a
    // full-resolution image into memory for a small thumbnail.
    if (request.targetSize.isValid()) {
        reader.setScaledSize(reader.size().scaled(request.targetSize, Qt::KeepAspectRatio));
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        if (error) {
            *error = core::Error(core::ErrorCode::CorruptData,
                                 QStringLiteral("Failed to decode image: %1 — %2")
                                         .arg(request.absolutePath, reader.errorString()));
        }
        return {};
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "jpeg", 85)) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("Failed to encode thumbnail as JPEG"));
        }
        return {};
    }

    core::MediaResult result;
    result.bytes = bytes;
    result.format = QStringLiteral("jpeg");
    result.actualSize = image.size();
    return result;
}

} // namespace pimio::thumbnail
