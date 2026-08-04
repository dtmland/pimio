#include "media_signature.h"

#include <QFileInfo>
#include <QSet>
#include <QString>

namespace pimio::metadata {

namespace {

/// Brands that mark an ISO base media file as a HEIF-family still image rather
/// than a movie. AVIF and HEIC are AV1/HEVC pictures wrapped in an ISO-BMFF
/// container; they share the `.mp4`-style box structure but carry no timed
/// video track, so treating them as movies makes the app try to play a picture.
bool isHeifImageBrand(const QByteArray &brand)
{
    static const QSet<QByteArray> imageBrands{
        QByteArrayLiteral("avif"), // AV1 still image
        QByteArrayLiteral("avis"), // AV1 image sequence (animated still)
        QByteArrayLiteral("heic"), // HEVC still image
        QByteArrayLiteral("heix"), // HEVC still image (10-bit/other profiles)
        QByteArrayLiteral("heim"), // HEVC multiview still image
        QByteArrayLiteral("heis"), // HEVC scalable still image
        QByteArrayLiteral("hevc"), // HEVC image sequence
        QByteArrayLiteral("heif"), // Generic HEIF image
        QByteArrayLiteral("mif1"), // HEIF baseline image
        QByteArrayLiteral("msf1"), // HEIF image sequence
        QByteArrayLiteral("miaf"), // Multi-Image Application Format (image)
    };
    return imageBrands.contains(brand);
}

/// True when any brand in a `ftyp` box (major brand plus the compatible brands
/// that fit within \a header) marks the file as a HEIF-family image.
bool ftypDeclaresImage(const QByteArray &header)
{
    // Layout: size(4) 'ftyp'(4) major(4) minor(4) then compatible brands(4 each).
    for (qsizetype offset = 8; offset + 4 <= header.size(); offset += 4) {
        if (offset == 12) {
            // Bytes 12..15 are the minor version, not a brand.
            continue;
        }
        if (isHeifImageBrand(header.mid(offset, 4))) {
            return true;
        }
    }
    return false;
}

} // namespace

MediaSignature signatureOf(const QByteArray &header)
{
    if (header.size() >= 3 && header.startsWith(QByteArrayLiteral("\xFF\xD8\xFF"))) {
        return MediaSignature::Jpeg;
    }
    if (header.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1A\n"))) {
        return MediaSignature::Png;
    }
    if (header.startsWith(QByteArrayLiteral("II\x2A\x00"))
        || header.startsWith(QByteArrayLiteral("MM\x00\x2A"))) {
        return MediaSignature::Tiff;
    }
    // ISO base media files start with a box; the brand box is not required to
    // be first, but every file pimio indexes has it there in practice, and
    // guessing further would mean scanning arbitrary data as box headers.
    if (header.size() >= 8 && header.mid(4, 4) == QByteArrayLiteral("ftyp")) {
        // AVIF/HEIC are ISO-BMFF containers too, but they hold a picture, not a
        // movie. The `ftyp` brand is what distinguishes them.
        if (ftypDeclaresImage(header)) {
            return MediaSignature::HeifImage;
        }
        return MediaSignature::IsoBmff;
    }
    return MediaSignature::Unknown;
}

bool hasMediaExtension(const QString &fileName)
{
    static const QSet<QString> extensions{
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("tiff"), QStringLiteral("tif"),  QStringLiteral("bmp"),
        QStringLiteral("gif"),  QStringLiteral("webp"), QStringLiteral("heic"),
        QStringLiteral("heif"), QStringLiteral("avif"), QStringLiteral("cr2"),
        QStringLiteral("cr3"),  QStringLiteral("nef"),  QStringLiteral("arw"),
        QStringLiteral("raf"),  QStringLiteral("dng"),  QStringLiteral("orf"),
        QStringLiteral("rw2"),  QStringLiteral("pef"),  QStringLiteral("mp4"),
        QStringLiteral("mov"),  QStringLiteral("avi"),  QStringLiteral("mkv"),
        QStringLiteral("m4v"),  QStringLiteral("wmv"),  QStringLiteral("flv"),
        QStringLiteral("webm"), QStringLiteral("3gp"),  QStringLiteral("mts"),
        QStringLiteral("m2ts"),
    };
    return extensions.contains(QFileInfo(fileName).suffix().toLower());
}

} // namespace pimio::metadata
