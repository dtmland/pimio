#include "iso_bmff_reader.h"

#include <QDateTime>
#include <QtEndian>

#include <limits>

namespace pimio::metadata {
namespace {

constexpr int kMaxNestingDepth = 8;

/// Seconds between the ISO base media epoch (1904-01-01 UTC) and the Unix
/// epoch.
constexpr qint64 kIsoEpochToUnixSeconds = 2082844800LL;

bool readU32(const QByteArray &bytes, qsizetype offset, quint32 *value)
{
    if (offset < 0 || offset + 4 > bytes.size()) {
        return false;
    }
    *value = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()) + offset);
    return true;
}

bool readU64(const QByteArray &bytes, qsizetype offset, quint64 *value)
{
    if (offset < 0 || offset + 8 > bytes.size()) {
        return false;
    }
    *value = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(bytes.constData()) + offset);
    return true;
}

struct Box
{
    QByteArray type;
    qsizetype payloadOffset = 0;
    qsizetype payloadSize = 0;
    qsizetype nextOffset = 0;
};

/// Reads the box header at \a offset. Returns false when the header or its
/// declared size does not fit in \a bytes.
bool readBox(const QByteArray &bytes, qsizetype offset, qsizetype limit, Box *box)
{
    quint32 size32 = 0;
    if (offset + 8 > limit || !readU32(bytes, offset, &size32)) {
        return false;
    }
    box->type = bytes.mid(offset + 4, 4);

    qsizetype headerSize = 8;
    qint64 totalSize = size32;
    if (size32 == 1) {
        quint64 size64 = 0;
        if (!readU64(bytes, offset + 8, &size64)) {
            return false;
        }
        headerSize = 16;
        if (size64 > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
            return false;
        }
        totalSize = static_cast<qint64>(size64);
    } else if (size32 == 0) {
        totalSize = limit - offset; // Extends to the end of the enclosing box.
    }

    if (totalSize < headerSize || offset + totalSize > limit) {
        return false;
    }
    box->payloadOffset = offset + headerSize;
    box->payloadSize = static_cast<qsizetype>(totalSize) - headerSize;
    box->nextOffset = offset + static_cast<qsizetype>(totalSize);
    return true;
}

void readMovieHeader(const QByteArray &bytes, const Box &box, FieldSet *fields)
{
    quint32 versionAndFlags = 0;
    if (!readU32(bytes, box.payloadOffset, &versionAndFlags)) {
        return;
    }
    const quint32 version = versionAndFlags >> 24;

    qint64 creationSeconds = 0;
    quint32 timescale = 0;
    quint64 duration = 0;
    if (version == 1) {
        quint64 creation = 0;
        if (!readU64(bytes, box.payloadOffset + 4, &creation)
            || !readU32(bytes, box.payloadOffset + 20, &timescale)
            || !readU64(bytes, box.payloadOffset + 24, &duration)) {
            return;
        }
        creationSeconds = static_cast<qint64>(creation);
    } else {
        quint32 creation = 0;
        quint32 duration32 = 0;
        if (!readU32(bytes, box.payloadOffset + 4, &creation)
            || !readU32(bytes, box.payloadOffset + 12, &timescale)
            || !readU32(bytes, box.payloadOffset + 16, &duration32)) {
            return;
        }
        creationSeconds = creation;
        duration = duration32;
    }

    if (timescale > 0 && duration > 0) {
        fields->durationMs =
                static_cast<qint64>(duration * 1000ULL / static_cast<quint64>(timescale));
    }

    // A zero creation time is the "not recorded" placeholder, not 1904.
    if (creationSeconds > 0) {
        const QDateTime utc = QDateTime::fromSecsSinceEpoch(
                creationSeconds - kIsoEpochToUnixSeconds, Qt::UTC);
        if (utc.isValid()) {
            // The container stores an instant in UTC, so the offset is known.
            fields->captureTime = core::CaptureTime::fromOffset(utc, 0);
        }
    }
}

void readTrackHeader(const QByteArray &bytes, const Box &box, FieldSet *fields)
{
    quint32 versionAndFlags = 0;
    if (!readU32(bytes, box.payloadOffset, &versionAndFlags)) {
        return;
    }
    const quint32 version = versionAndFlags >> 24;
    // Fixed layout up to the 16.16 display width and height at the end.
    const qsizetype dimensionsOffset =
            box.payloadOffset + box.payloadSize - 8;
    if (box.payloadSize < (version == 1 ? 92 : 80)) {
        return;
    }

    quint32 width = 0;
    quint32 height = 0;
    if (!readU32(bytes, dimensionsOffset, &width)
        || !readU32(bytes, dimensionsOffset + 4, &height)) {
        return;
    }
    const int pixelWidth = static_cast<int>(width >> 16);
    const int pixelHeight = static_cast<int>(height >> 16);
    if (pixelWidth > 0 && pixelHeight > 0) {
        fields->pixelWidth = pixelWidth;
        fields->pixelHeight = pixelHeight;
    }
}

void walk(const QByteArray &bytes, qsizetype offset, qsizetype limit, int depth,
          FieldSet *fields, bool *sawTrack);

void walkTrack(const QByteArray &bytes, qsizetype offset, qsizetype limit, int depth,
               FieldSet *fields)
{
    if (depth > kMaxNestingDepth) {
        return;
    }
    Box box;
    while (offset < limit && readBox(bytes, offset, limit, &box)) {
        if (box.type == QByteArrayLiteral("tkhd")) {
            FieldSet trackFields;
            readTrackHeader(bytes, box, &trackFields);
            // Only a visual track contributes display dimensions, and the
            // first one wins; sound tracks carry a zero-sized header.
            if (trackFields.pixelWidth.has_value() && !fields->pixelWidth.has_value()) {
                fields->pixelWidth = trackFields.pixelWidth;
                fields->pixelHeight = trackFields.pixelHeight;
            }
        } else if (box.type == QByteArrayLiteral("hdlr")) {
            const QByteArray handler = bytes.mid(box.payloadOffset + 8, 4);
            if (handler == QByteArrayLiteral("soun")) {
                fields->hasAudio = true;
            }
        } else if (box.type == QByteArrayLiteral("mdia")
                   || box.type == QByteArrayLiteral("minf")) {
            walkTrack(bytes, box.payloadOffset, box.payloadOffset + box.payloadSize, depth + 1,
                      fields);
        }
        offset = box.nextOffset;
    }
}

void walk(const QByteArray &bytes, qsizetype offset, qsizetype limit, int depth,
          FieldSet *fields, bool *sawTrack)
{
    if (depth > kMaxNestingDepth) {
        return;
    }
    Box box;
    while (offset < limit && readBox(bytes, offset, limit, &box)) {
        if (box.type == QByteArrayLiteral("moov")) {
            walk(bytes, box.payloadOffset, box.payloadOffset + box.payloadSize, depth + 1, fields,
                 sawTrack);
        } else if (box.type == QByteArrayLiteral("mvhd")) {
            readMovieHeader(bytes, box, fields);
        } else if (box.type == QByteArrayLiteral("trak")) {
            *sawTrack = true;
            walkTrack(bytes, box.payloadOffset, box.payloadOffset + box.payloadSize, depth + 1,
                      fields);
        }
        offset = box.nextOffset;
    }
}

} // namespace

bool readIsoBmff(const QByteArray &bytes, FieldSet *fields, QStringList *warnings)
{
    if (bytes.size() < 8 || bytes.mid(4, 4) != QByteArrayLiteral("ftyp")) {
        return false;
    }

    bool sawTrack = false;
    walk(bytes, 0, bytes.size(), 0, fields, &sawTrack);

    if (!fields->durationMs.has_value()) {
        warnings->append(QStringLiteral("The movie header is missing or unreadable; the "
                                        "duration is unknown."));
    }
    // A file with no tracks cannot be shown to carry audio, and saying "no
    // audio" would be a claim the file does not support.
    if (sawTrack && !fields->hasAudio.has_value()) {
        fields->hasAudio = false;
    }
    return true;
}

} // namespace pimio::metadata
