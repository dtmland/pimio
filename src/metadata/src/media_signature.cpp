#include "media_signature.h"

#include <QFileInfo>
#include <QSet>
#include <QString>

namespace pimio::metadata {

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
