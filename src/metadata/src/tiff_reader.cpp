#include "image_reader.h"
#include "tiff_reader_private.h"

#include <QDateTime>
#include <QtEndian>

#include <cmath>

namespace pimio::metadata {
namespace {

/// Bounds-checked accessors. Every offset in these formats comes from the file
/// itself, so all of them are treated as untrusted.
bool readU8(const QByteArray &bytes, qsizetype offset, quint8 *value)
{
    if (offset < 0 || offset + 1 > bytes.size()) {
        return false;
    }
    *value = static_cast<quint8>(bytes.at(offset));
    return true;
}

bool readU16(const QByteArray &bytes, qsizetype offset, bool bigEndian, quint16 *value)
{
    if (offset < 0 || offset + 2 > bytes.size()) {
        return false;
    }
    const uchar *data = reinterpret_cast<const uchar *>(bytes.constData()) + offset;
    *value = bigEndian ? qFromBigEndian<quint16>(data) : qFromLittleEndian<quint16>(data);
    return true;
}

bool readU32(const QByteArray &bytes, qsizetype offset, bool bigEndian, quint32 *value)
{
    if (offset < 0 || offset + 4 > bytes.size()) {
        return false;
    }
    const uchar *data = reinterpret_cast<const uchar *>(bytes.constData()) + offset;
    *value = bigEndian ? qFromBigEndian<quint32>(data) : qFromLittleEndian<quint32>(data);
    return true;
}

constexpr quint16 kTypeByte = 1;
constexpr quint16 kTypeAscii = 2;
constexpr quint16 kTypeShort = 3;
constexpr quint16 kTypeLong = 4;
constexpr quint16 kTypeRational = 5;
constexpr quint16 kTypeUndefined = 7;
constexpr quint16 kTypeSlong = 9;
constexpr quint16 kTypeSrational = 10;

int sizeOfType(quint16 type)
{
    switch (type) {
    case kTypeByte:
    case kTypeAscii:
    case kTypeUndefined:
        return 1;
    case kTypeShort:
        return 2;
    case kTypeLong:
    case kTypeSlong:
        return 4;
    case kTypeRational:
    case kTypeSrational:
        return 8;
    default:
        return 0;
    }
}

/// One directory entry with its payload already resolved and bounds-checked.
struct TiffEntry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    QByteArray value;
};

/// An upper bound on directory entries. A real file has tens; anything beyond
/// this is a damaged length that would otherwise drive a long parse.
constexpr quint16 kMaxIfdEntries = 512;

/// Reads one image file directory. Returns false when the directory header
/// itself is unusable; individual unreadable entries are skipped.
bool readIfd(const QByteArray &tiff, quint32 ifdOffset, bool bigEndian,
             QList<TiffEntry> *entries)
{
    quint16 entryCount = 0;
    if (!readU16(tiff, ifdOffset, bigEndian, &entryCount)) {
        return false;
    }
    if (entryCount > kMaxIfdEntries) {
        return false;
    }

    for (quint16 index = 0; index < entryCount; ++index) {
        const qsizetype entryOffset = static_cast<qsizetype>(ifdOffset) + 2 + 12 * index;
        TiffEntry entry;
        quint32 valueOffset = 0;
        if (!readU16(tiff, entryOffset, bigEndian, &entry.tag)
            || !readU16(tiff, entryOffset + 2, bigEndian, &entry.type)
            || !readU32(tiff, entryOffset + 4, bigEndian, &entry.count)
            || !readU32(tiff, entryOffset + 8, bigEndian, &valueOffset)) {
            return false;
        }

        const int unitSize = sizeOfType(entry.type);
        if (unitSize == 0) {
            continue; // Unknown type: not something this build understands.
        }
        const qint64 byteCount = static_cast<qint64>(unitSize) * entry.count;
        if (byteCount < 0 || byteCount > tiff.size()) {
            continue;
        }

        if (byteCount <= 4) {
            entry.value = tiff.mid(entryOffset + 8, static_cast<qsizetype>(byteCount));
        } else {
            if (static_cast<qint64>(valueOffset) + byteCount > tiff.size()) {
                continue;
            }
            entry.value = tiff.mid(static_cast<qsizetype>(valueOffset),
                                   static_cast<qsizetype>(byteCount));
        }
        if (entry.value.size() != byteCount) {
            continue;
        }
        entries->append(entry);
    }
    return true;
}

QString asciiValue(const TiffEntry &entry)
{
    if (entry.type != kTypeAscii) {
        return {};
    }
    QByteArray text = entry.value;
    const qsizetype terminator = text.indexOf('\0');
    if (terminator >= 0) {
        text.truncate(terminator);
    }
    return QString::fromLatin1(text).trimmed();
}

std::optional<quint32> integerValue(const TiffEntry &entry, bool bigEndian)
{
    quint16 shortValue = 0;
    quint32 longValue = 0;
    switch (entry.type) {
    case kTypeShort:
        if (readU16(entry.value, 0, bigEndian, &shortValue)) {
            return shortValue;
        }
        return std::nullopt;
    case kTypeLong:
    case kTypeSlong:
        if (readU32(entry.value, 0, bigEndian, &longValue)) {
            return longValue;
        }
        return std::nullopt;
    case kTypeByte:
        if (!entry.value.isEmpty()) {
            return static_cast<quint32>(static_cast<quint8>(entry.value.at(0)));
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

/// Reads \a index'th rational as a double. Returns nothing when the
/// denominator is zero, which is how damaged GPS blocks usually present.
std::optional<double> rationalAt(const TiffEntry &entry, quint32 index, bool bigEndian)
{
    if (entry.type != kTypeRational && entry.type != kTypeSrational) {
        return std::nullopt;
    }
    quint32 numerator = 0;
    quint32 denominator = 0;
    const qsizetype offset = static_cast<qsizetype>(index) * 8;
    if (!readU32(entry.value, offset, bigEndian, &numerator)
        || !readU32(entry.value, offset + 4, bigEndian, &denominator)) {
        return std::nullopt;
    }
    if (denominator == 0) {
        return std::nullopt;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

std::optional<double> coordinateFrom(const TiffEntry &entry, bool bigEndian)
{
    if (entry.count < 3) {
        return std::nullopt;
    }
    const auto degrees = rationalAt(entry, 0, bigEndian);
    const auto minutes = rationalAt(entry, 1, bigEndian);
    const auto seconds = rationalAt(entry, 2, bigEndian);
    if (!degrees.has_value() || !minutes.has_value() || !seconds.has_value()) {
        return std::nullopt;
    }
    return *degrees + *minutes / 60.0 + *seconds / 3600.0;
}

/// Parses EXIF's "YYYY:MM:DD HH:MM:SS". Anything else, including the all-zero
/// placeholder some cameras write, is treated as absent.
QDateTime parseExifDateTime(const QString &text)
{
    if (text.isEmpty() || text.startsWith(QLatin1String("0000"))) {
        return {};
    }
    return QDateTime::fromString(text, QStringLiteral("yyyy:MM:dd HH:mm:ss"));
}

/// Parses EXIF's "+HH:MM" offset into seconds.
std::optional<int> parseExifOffset(const QString &text)
{
    if (text.size() < 6) {
        return std::nullopt;
    }
    const QChar sign = text.at(0);
    if (sign != QLatin1Char('+') && sign != QLatin1Char('-')) {
        return std::nullopt;
    }
    bool hoursOk = false;
    bool minutesOk = false;
    const int hours = QStringView(text).mid(1, 2).toInt(&hoursOk);
    const int minutes = QStringView(text).mid(4, 2).toInt(&minutesOk);
    if (!hoursOk || !minutesOk || hours > 23 || minutes > 59) {
        return std::nullopt;
    }
    const int seconds = hours * 3600 + minutes * 60;
    return sign == QLatin1Char('-') ? -seconds : seconds;
}

const TiffEntry *find(const QList<TiffEntry> &entries, quint16 tag)
{
    for (const TiffEntry &entry : entries) {
        if (entry.tag == tag) {
            return &entry;
        }
    }
    return nullptr;
}

void applyGps(const QList<TiffEntry> &gps, bool bigEndian, FieldSet *fields)
{
    const TiffEntry *latitudeRef = find(gps, 0x0001);
    const TiffEntry *latitude = find(gps, 0x0002);
    const TiffEntry *longitudeRef = find(gps, 0x0003);
    const TiffEntry *longitude = find(gps, 0x0004);
    if (latitude == nullptr || longitude == nullptr) {
        return;
    }

    auto latitudeDegrees = coordinateFrom(*latitude, bigEndian);
    auto longitudeDegrees = coordinateFrom(*longitude, bigEndian);
    if (!latitudeDegrees.has_value() || !longitudeDegrees.has_value()) {
        return;
    }
    if (latitudeRef != nullptr && asciiValue(*latitudeRef).startsWith(QLatin1Char('S'))) {
        latitudeDegrees = -*latitudeDegrees;
    }
    if (longitudeRef != nullptr && asciiValue(*longitudeRef).startsWith(QLatin1Char('W'))) {
        longitudeDegrees = -*longitudeDegrees;
    }

    auto location = core::GeoLocation::create(*latitudeDegrees, *longitudeDegrees);
    if (!location.has_value()) {
        return;
    }

    if (const TiffEntry *altitude = find(gps, 0x0006)) {
        if (const auto metres = rationalAt(*altitude, 0, bigEndian)) {
            double value = *metres;
            const TiffEntry *reference = find(gps, 0x0005);
            if (reference != nullptr) {
                if (const auto belowSeaLevel = integerValue(*reference, bigEndian)) {
                    if (*belowSeaLevel == 1) {
                        value = -value;
                    }
                }
            }
            location->setAltitudeMetres(value);
        }
    }

    fields->location = location;
}

} // namespace

namespace detail {

void applyPrimaryTiffDimensions(const QByteArray &bytes, FieldSet *fields)
{
    // A bare TIFF records its dimensions in the primary directory rather than
    // in the EXIF sub-directory.
    const bool bigEndian = bytes.startsWith(QByteArrayLiteral("MM"));
    quint32 firstIfdOffset = 0;
    if (!readU32(bytes, 4, bigEndian, &firstIfdOffset)) {
        return;
    }
    QList<TiffEntry> ifd0;
    if (!readIfd(bytes, firstIfdOffset, bigEndian, &ifd0)) {
        return;
    }
    if (const TiffEntry *width = find(ifd0, 0x0100)) {
        if (const auto value = integerValue(*width, bigEndian); value.has_value() && *value > 0) {
            fields->pixelWidth = static_cast<int>(*value);
        }
    }
    if (const TiffEntry *height = find(ifd0, 0x0101)) {
        if (const auto value = integerValue(*height, bigEndian); value.has_value() && *value > 0) {
            fields->pixelHeight = static_cast<int>(*value);
        }
    }
}

} // namespace detail

int rotationForExifOrientation(int orientation)
{
    switch (orientation) {
    case 1:
    case 2:
        return 0;
    case 3:
    case 4:
        return 180;
    case 5:
    case 6:
        return 90;
    case 7:
    case 8:
        return 270;
    default:
        return 0;
    }
}

bool readExifTiffBlock(const QByteArray &tiff, FieldSet *fields, QStringList *warnings)
{
    bool bigEndian = false;
    if (tiff.startsWith(QByteArrayLiteral("MM"))) {
        bigEndian = true;
    } else if (!tiff.startsWith(QByteArrayLiteral("II"))) {
        warnings->append(QStringLiteral("The EXIF block has no byte-order marker."));
        return false;
    }

    quint16 magic = 0;
    quint32 firstIfdOffset = 0;
    if (!readU16(tiff, 2, bigEndian, &magic) || magic != 42
        || !readU32(tiff, 4, bigEndian, &firstIfdOffset)) {
        warnings->append(QStringLiteral("The EXIF block is not a readable TIFF header."));
        return false;
    }

    QList<TiffEntry> ifd0;
    if (!readIfd(tiff, firstIfdOffset, bigEndian, &ifd0)) {
        warnings->append(QStringLiteral("The EXIF primary directory is unreadable."));
        return false;
    }

    QList<TiffEntry> exifIfd;
    if (const TiffEntry *pointer = find(ifd0, 0x8769)) {
        if (const auto offset = integerValue(*pointer, bigEndian)) {
            if (!readIfd(tiff, *offset, bigEndian, &exifIfd)) {
                warnings->append(QStringLiteral("The EXIF sub-directory is unreadable."));
            }
        }
    }

    QList<TiffEntry> gpsIfd;
    if (const TiffEntry *pointer = find(ifd0, 0x8825)) {
        if (const auto offset = integerValue(*pointer, bigEndian)) {
            if (!readIfd(tiff, *offset, bigEndian, &gpsIfd)) {
                warnings->append(QStringLiteral("The EXIF GPS directory is unreadable."));
            }
        }
    }

    if (const TiffEntry *make = find(ifd0, 0x010F)) {
        const QString value = asciiValue(*make);
        if (!value.isEmpty()) {
            fields->cameraMake = value;
        }
    }
    if (const TiffEntry *model = find(ifd0, 0x0110)) {
        const QString value = asciiValue(*model);
        if (!value.isEmpty()) {
            fields->cameraModel = value;
        }
    }
    if (const TiffEntry *lens = find(exifIfd, 0xA434)) {
        const QString value = asciiValue(*lens);
        if (!value.isEmpty()) {
            fields->lensModel = value;
        }
    }
    if (const TiffEntry *orientation = find(ifd0, 0x0112)) {
        if (const auto value = integerValue(*orientation, bigEndian)) {
            fields->rotationDegrees = rotationForExifOrientation(static_cast<int>(*value));
        }
    }

    // The EXIF sub-directory records the stored pixel dimensions. They are the
    // authority for JPEG only when the frame header is missing, so they are
    // recorded and left for the caller's precedence to resolve.
    if (const TiffEntry *width = find(exifIfd, 0xA002)) {
        if (const auto value = integerValue(*width, bigEndian); value.has_value() && *value > 0) {
            fields->pixelWidth = static_cast<int>(*value);
        }
    }
    if (const TiffEntry *height = find(exifIfd, 0xA003)) {
        if (const auto value = integerValue(*height, bigEndian); value.has_value() && *value > 0) {
            fields->pixelHeight = static_cast<int>(*value);
        }
    }

    QDateTime wallClock;
    if (const TiffEntry *original = find(exifIfd, 0x9003)) {
        wallClock = parseExifDateTime(asciiValue(*original));
    }
    if (!wallClock.isValid()) {
        if (const TiffEntry *modified = find(ifd0, 0x0132)) {
            wallClock = parseExifDateTime(asciiValue(*modified));
        }
    }
    if (wallClock.isValid()) {
        std::optional<int> offsetSeconds;
        if (const TiffEntry *offset = find(exifIfd, 0x9011)) {
            offsetSeconds = parseExifOffset(asciiValue(*offset));
        }
        if (!offsetSeconds.has_value()) {
            if (const TiffEntry *offset = find(exifIfd, 0x9010)) {
                offsetSeconds = parseExifOffset(asciiValue(*offset));
            }
        }
        // A missing offset stays missing. Assuming UTC here would turn an
        // unknown into a fact the rest of pimio could never question again.
        fields->captureTime = offsetSeconds.has_value()
                ? core::CaptureTime::fromOffset(wallClock, *offsetSeconds)
                : core::CaptureTime::fromLocalWallClock(wallClock);
    }

    applyGps(gpsIfd, bigEndian, fields);
    return true;
}

} // namespace pimio::metadata
