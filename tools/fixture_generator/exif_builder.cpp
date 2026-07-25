#include "exif_builder.h"

#include <QList>
#include <QtEndian>

#include <cmath>

namespace pimio::fixtures {
namespace {

constexpr quint16 kTypeAscii = 2;
constexpr quint16 kTypeShort = 3;
constexpr quint16 kTypeLong = 4;
constexpr quint16 kTypeRational = 5;

void appendU16(QByteArray &target, quint16 value)
{
    char buffer[2];
    qToLittleEndian(value, buffer);
    target.append(buffer, 2);
}

void appendU32(QByteArray &target, quint32 value)
{
    char buffer[4];
    qToLittleEndian(value, buffer);
    target.append(buffer, 4);
}

struct Entry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    /// Inline value when the payload fits in four bytes, otherwise empty.
    QByteArray inlineValue;
    /// Payload written to the data area when it does not fit inline.
    QByteArray externalValue;
};

Entry asciiEntry(quint16 tag, const QByteArray &text)
{
    Entry entry;
    entry.tag = tag;
    entry.type = kTypeAscii;
    QByteArray value = text;
    value.append('\0');
    entry.count = static_cast<quint32>(value.size());
    if (value.size() <= 4) {
        value.append(4 - value.size(), '\0');
        entry.inlineValue = value;
    } else {
        entry.externalValue = value;
    }
    return entry;
}

Entry shortEntry(quint16 tag, quint16 value)
{
    Entry entry;
    entry.tag = tag;
    entry.type = kTypeShort;
    entry.count = 1;
    QByteArray inlineValue;
    appendU16(inlineValue, value);
    inlineValue.append(2, '\0');
    entry.inlineValue = inlineValue;
    return entry;
}

Entry longEntry(quint16 tag, quint32 value)
{
    Entry entry;
    entry.tag = tag;
    entry.type = kTypeLong;
    entry.count = 1;
    QByteArray inlineValue;
    appendU32(inlineValue, value);
    entry.inlineValue = inlineValue;
    return entry;
}

/// Encodes a coordinate as the degrees/minutes/seconds rationals EXIF expects.
Entry rationalCoordinateEntry(quint16 tag, double degreesValue)
{
    const double absolute = std::abs(degreesValue);
    const quint32 degrees = static_cast<quint32>(absolute);
    const double minutesValue = (absolute - degrees) * 60.0;
    const quint32 minutes = static_cast<quint32>(minutesValue);
    const quint32 seconds = static_cast<quint32>((minutesValue - minutes) * 60.0 * 1000.0 + 0.5);

    Entry entry;
    entry.tag = tag;
    entry.type = kTypeRational;
    entry.count = 3;

    QByteArray value;
    appendU32(value, degrees);
    appendU32(value, 1);
    appendU32(value, minutes);
    appendU32(value, 1);
    appendU32(value, seconds);
    appendU32(value, 1000);
    entry.externalValue = value;
    return entry;
}

quint32 ifdSize(int entryCount)
{
    return static_cast<quint32>(2 + 12 * entryCount + 4);
}

void appendIfd(QByteArray &target, const QList<Entry> &entries, QByteArray &dataArea,
               quint32 dataAreaBaseOffset)
{
    appendU16(target, static_cast<quint16>(entries.size()));
    for (const Entry &entry : entries) {
        appendU16(target, entry.tag);
        appendU16(target, entry.type);
        appendU32(target, entry.count);
        if (entry.externalValue.isEmpty()) {
            target.append(entry.inlineValue);
        } else {
            appendU32(target, dataAreaBaseOffset + static_cast<quint32>(dataArea.size()));
            dataArea.append(entry.externalValue);
            if (dataArea.size() % 2 != 0) {
                dataArea.append('\0');
            }
        }
    }
    appendU32(target, 0);
}

} // namespace

QByteArray buildExifApp1(const ExifSpec &spec)
{
    QList<Entry> ifd0;
    if (!spec.make.isEmpty()) {
        ifd0.append(asciiEntry(0x010F, spec.make.toLatin1()));
    }
    if (!spec.model.isEmpty()) {
        ifd0.append(asciiEntry(0x0110, spec.model.toLatin1()));
    }
    ifd0.append(shortEntry(0x0112, spec.orientation));

    QList<Entry> exifIfd;
    if (!spec.dateTimeOriginal.isEmpty()) {
        exifIfd.append(asciiEntry(0x9003, spec.dateTimeOriginal.toLatin1()));
    }
    if (!spec.offsetTimeOriginal.isEmpty()) {
        exifIfd.append(asciiEntry(0x9011, spec.offsetTimeOriginal.toLatin1()));
    }

    QList<Entry> gpsIfd;
    if (spec.hasGps) {
        gpsIfd.append(asciiEntry(0x0001,
                                 spec.latitude >= 0 ? QByteArrayLiteral("N")
                                                    : QByteArrayLiteral("S")));
        gpsIfd.append(rationalCoordinateEntry(0x0002, spec.latitude));
        gpsIfd.append(asciiEntry(0x0003,
                                 spec.longitude >= 0 ? QByteArrayLiteral("E")
                                                     : QByteArrayLiteral("W")));
        gpsIfd.append(rationalCoordinateEntry(0x0004, spec.longitude));
    }

    // Reserve the pointer entries before computing offsets so that the sizes
    // used below match what is actually written.
    const bool hasExifIfd = !exifIfd.isEmpty();
    const int ifd0Count = ifd0.size() + (hasExifIfd ? 1 : 0) + (spec.hasGps ? 1 : 0);

    constexpr quint32 kTiffHeaderSize = 8;
    const quint32 exifIfdOffset = kTiffHeaderSize + ifdSize(ifd0Count);
    const quint32 gpsIfdOffset =
            exifIfdOffset + (hasExifIfd ? ifdSize(exifIfd.size()) : 0);
    const quint32 dataAreaOffset = gpsIfdOffset + (spec.hasGps ? ifdSize(gpsIfd.size()) : 0);

    if (hasExifIfd) {
        ifd0.append(longEntry(0x8769, exifIfdOffset));
    }
    if (spec.hasGps) {
        ifd0.append(longEntry(0x8825, gpsIfdOffset));
    }
    Q_ASSERT(ifd0.size() == ifd0Count);

    QByteArray tiff;
    tiff.append("II", 2);
    appendU16(tiff, 0x002A);
    appendU32(tiff, kTiffHeaderSize);

    QByteArray dataArea;
    appendIfd(tiff, ifd0, dataArea, dataAreaOffset);
    if (hasExifIfd) {
        appendIfd(tiff, exifIfd, dataArea, dataAreaOffset);
    }
    if (spec.hasGps) {
        appendIfd(tiff, gpsIfd, dataArea, dataAreaOffset);
    }
    tiff.append(dataArea);

    QByteArray app1;
    app1.append("Exif\0\0", 6);
    app1.append(tiff);

    QByteArray segment;
    segment.append(static_cast<char>(0xFF));
    segment.append(static_cast<char>(0xE1));
    const quint16 segmentLength = static_cast<quint16>(app1.size() + 2);
    segment.append(static_cast<char>((segmentLength >> 8) & 0xFF));
    segment.append(static_cast<char>(segmentLength & 0xFF));
    segment.append(app1);
    return segment;
}

QByteArray injectExif(const QByteArray &jpeg, const QByteArray &app1Segment)
{
    if (jpeg.size() < 2 || static_cast<quint8>(jpeg.at(0)) != 0xFF
        || static_cast<quint8>(jpeg.at(1)) != 0xD8) {
        return QByteArray();
    }

    // Drop any APP0/APP1 segment the encoder emitted so the EXIF block is the
    // first application segment, which is what readers expect.
    int position = 2;
    while (position + 4 <= jpeg.size() && static_cast<quint8>(jpeg.at(position)) == 0xFF) {
        const quint8 marker = static_cast<quint8>(jpeg.at(position + 1));
        if (marker != 0xE0 && marker != 0xE1) {
            break;
        }
        const int length = (static_cast<quint8>(jpeg.at(position + 2)) << 8)
                | static_cast<quint8>(jpeg.at(position + 3));
        position += 2 + length;
    }

    QByteArray result;
    result.append(jpeg.left(2));
    result.append(app1Segment);
    result.append(jpeg.mid(position));
    return result;
}

} // namespace pimio::fixtures
