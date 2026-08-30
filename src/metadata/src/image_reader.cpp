#include "image_reader.h"
#include "tiff_reader_private.h"

#include <QtEndian>

namespace pimio::metadata {
namespace {

bool readBeU16(const QByteArray &bytes, qsizetype offset, quint16 *value)
{
    if (offset < 0 || offset + 2 > bytes.size()) {
        return false;
    }
    const uchar *data = reinterpret_cast<const uchar *>(bytes.constData()) + offset;
    *value = qFromBigEndian<quint16>(data);
    return true;
}

bool readBeU32(const QByteArray &bytes, qsizetype offset, quint32 *value)
{
    if (offset < 0 || offset + 4 > bytes.size()) {
        return false;
    }
    const uchar *data = reinterpret_cast<const uchar *>(bytes.constData()) + offset;
    *value = qFromBigEndian<quint32>(data);
    return true;
}

} // namespace

bool readJpeg(const QByteArray &bytes, FieldSet *fields, QStringList *warnings)
{
    if (bytes.size() < 4 || static_cast<quint8>(bytes.at(0)) != 0xFF
        || static_cast<quint8>(bytes.at(1)) != 0xD8) {
        return false;
    }

    bool sawFrameHeader = false;
    bool truncated = false;
    qsizetype position = 2;

    while (position + 1 < bytes.size()) {
        if (static_cast<quint8>(bytes.at(position)) != 0xFF) {
            truncated = true;
            break;
        }
        // Any number of 0xFF bytes may pad a marker.
        while (position + 1 < bytes.size()
               && static_cast<quint8>(bytes.at(position + 1)) == 0xFF) {
            ++position;
        }
        if (position + 1 >= bytes.size()) {
            truncated = true;
            break;
        }

        const quint8 marker = static_cast<quint8>(bytes.at(position + 1));
        position += 2;

        // Standalone markers carry no payload.
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) {
            break; // End of image, or start of entropy-coded scan data.
        }

        quint16 segmentLength = 0;
        if (!readBeU16(bytes, position, &segmentLength) || segmentLength < 2) {
            truncated = true;
            break;
        }
        const qsizetype payloadOffset = position + 2;
        const qsizetype payloadSize = segmentLength - 2;
        if (payloadOffset + payloadSize > bytes.size()) {
            truncated = true;
            break;
        }

        const bool isFrameHeader = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4
                && marker != 0xC8 && marker != 0xCC;
        if (isFrameHeader && payloadSize >= 5) {
            quint16 height = 0;
            quint16 width = 0;
            if (readBeU16(bytes, payloadOffset + 1, &height)
                && readBeU16(bytes, payloadOffset + 3, &width) && width > 0
                && height > 0) {
                fields->pixelWidth = width;
                fields->pixelHeight = height;
                sawFrameHeader = true;
            }
        } else if (marker == 0xE1
                   && bytes.mid(payloadOffset, 6) == QByteArrayLiteral("Exif\0\0")) {
            const QByteArray tiff = bytes.mid(payloadOffset + 6, payloadSize - 6);
            FieldSet exifFields;
            if (readExifTiffBlock(tiff, &exifFields, warnings)) {
                // Frame-header dimensions win: they describe the actual stored
                // image, whereas the EXIF values are only a claim about it.
                const auto width = fields->pixelWidth;
                const auto height = fields->pixelHeight;
                *fields = exifFields;
                if (width.has_value()) {
                    fields->pixelWidth = width;
                }
                if (height.has_value()) {
                    fields->pixelHeight = height;
                }
            } else {
                warnings->append(QStringLiteral(
                        "The EXIF metadata is damaged; the image is indexed without it."));
            }
        }

        position = payloadOffset + payloadSize;
    }

    if (!sawFrameHeader) {
        warnings->append(QStringLiteral("The JPEG data ends before its frame header."));
        return false;
    }
    if (truncated) {
        warnings->append(QStringLiteral("The JPEG data is truncated after its frame header."));
    }
    return true;
}

bool readPng(const QByteArray &bytes, FieldSet *fields, QStringList *warnings)
{
    if (!bytes.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1A\n"))) {
        return false;
    }

    qsizetype position = 8;
    bool sawHeader = false;
    while (position + 8 <= bytes.size()) {
        quint32 chunkLength = 0;
        if (!readBeU32(bytes, position, &chunkLength)) {
            break;
        }
        const QByteArray type = bytes.mid(position + 4, 4);
        const qsizetype payloadOffset = position + 8;
        if (chunkLength > static_cast<quint32>(bytes.size())
            || payloadOffset + static_cast<qsizetype>(chunkLength) > bytes.size()) {
            break;
        }

        if (type == QByteArrayLiteral("IHDR") && chunkLength >= 8) {
            quint32 width = 0;
            quint32 height = 0;
            if (readBeU32(bytes, payloadOffset, &width)
                && readBeU32(bytes, payloadOffset + 4, &height) && width > 0
                && height > 0) {
                fields->pixelWidth = static_cast<int>(width);
                fields->pixelHeight = static_cast<int>(height);
                sawHeader = true;
            }
        } else if (type == QByteArrayLiteral("eXIf")) {
            // PNG may carry a raw EXIF TIFF block. It is optional, so damage
            // here is a warning and never fails the file.
            FieldSet exifFields;
            if (readExifTiffBlock(bytes.mid(payloadOffset, static_cast<qsizetype>(chunkLength)),
                                  &exifFields, warnings)) {
                const auto width = fields->pixelWidth;
                const auto height = fields->pixelHeight;
                *fields = exifFields;
                fields->pixelWidth = width;
                fields->pixelHeight = height;
            }
        } else if (type == QByteArrayLiteral("IEND")) {
            break;
        }

        position = payloadOffset + static_cast<qsizetype>(chunkLength) + 4; // + CRC
    }

    if (!sawHeader) {
        warnings->append(QStringLiteral("The PNG header chunk is missing or unreadable."));
        return false;
    }
    return true;
}

bool readTiff(const QByteArray &bytes, FieldSet *fields, QStringList *warnings)
{
    if (!readExifTiffBlock(bytes, fields, warnings)) {
        return false;
    }
    detail::applyPrimaryTiffDimensions(bytes, fields);
    return true;
}

} // namespace pimio::metadata
