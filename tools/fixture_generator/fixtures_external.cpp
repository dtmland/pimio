#include "fixtures.h"

#include <QFile>
#include <QJsonObject>

#include <cstdio>

namespace pimio::fixtures {

bool appendExternalFixtureEntries(const QDir &outputDir, QJsonArray *entries)
{
    const QString relativePath = QStringLiteral("video/decodable-clip.mp4");
    QFile file(outputDir.absoluteFilePath(relativePath));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr,
                     "The committed external fixture %s is missing; refusing to omit it from "
                     "manifest.json.\n",
                     qPrintable(relativePath));
        return false;
    }
    const QByteArray contents = file.readAll();

    QJsonObject entry;
    entry.insert(QStringLiteral("path"), relativePath);
    entry.insert(QStringLiteral("sizeBytes"), contents.size());
    entry.insert(QStringLiteral("sha256"), sha256Of(contents));
    entry.insert(
            QStringLiteral("provenance"),
            QStringLiteral("Generated once with `ffmpeg -f lavfi -i "
                           "color=c=red:size=32x32:rate=4:duration=1 -frames:v 4 -pix_fmt "
                           "yuv420p -c:v libx264 -profile:v baseline -movflags +faststart`. "
                           "Synthetic test pattern owned by the pimio project; no third-party "
                           "media is included."));
    entry.insert(QStringLiteral("covers"),
                 QStringLiteral("A single actually decodable video frame, for thumbnail "
                                "rendering."));
    entry.insert(
            QStringLiteral("notes"),
            QStringLiteral("Baseline-profile H.264 in an MP4 container, 32x32, 4 frames of "
                           "solid red, no audio. Generated once with ffmpeg 6.1.1 (libx264), a "
                           "widely available open-source encoder used here only as an offline "
                           "fixture-generation tool; ffmpeg is not a build or runtime dependency "
                           "of pimio. Unlike structural.mp4 and audio-video.mp4, this file is "
                           "deliberately real: it is the corpus's evidence that "
                           "VideoFrameRenderer decodes actual video through Qt Multimedia rather "
                           "than only handling structurally-valid-but-empty containers."));
    entries->append(entry);

    const QString avifRelativePath = QStringLiteral("images/avif-solid.avif");
    QFile avifFile(outputDir.absoluteFilePath(avifRelativePath));
    if (!avifFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr,
                     "The committed external fixture %s is missing; refusing to omit it from "
                     "manifest.json.\n",
                     qPrintable(avifRelativePath));
        return false;
    }
    const QByteArray avifContents = avifFile.readAll();

    QJsonObject avifEntry;
    avifEntry.insert(QStringLiteral("path"), avifRelativePath);
    avifEntry.insert(QStringLiteral("sizeBytes"), avifContents.size());
    avifEntry.insert(QStringLiteral("sha256"), sha256Of(avifContents));
    avifEntry.insert(
            QStringLiteral("provenance"),
            QStringLiteral("Generated once by tools/fixture_generator with "
                           "qt-avif-image-plugin 0.10.3, libavif 1.4.2, and libaom 3.14.1. "
                           "Synthetic gradient owned by the pimio project; no third-party media "
                           "is included."));
    avifEntry.insert(
            QStringLiteral("covers"),
            QStringLiteral("A real AVIF image for thumbnail and detail-view decoding."));
    avifEntry.insert(
            QStringLiteral("notes"),
            QStringLiteral("Kept as an external fixture because pimio ships only the AV1 decoder; "
                           "fixture regeneration verifies and preserves this committed file."));
    entries->append(avifEntry);

    const QString heicRelativePath = QStringLiteral("images/heic-grid.heic");
    QFile heicFile(outputDir.absoluteFilePath(heicRelativePath));
    if (!heicFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr,
                     "The committed external fixture %s is missing; refusing to omit it from "
                     "manifest.json.\n",
                     qPrintable(heicRelativePath));
        return false;
    }
    const QByteArray heicContents = heicFile.readAll();

    QJsonObject heicEntry;
    heicEntry.insert(QStringLiteral("path"), heicRelativePath);
    heicEntry.insert(QStringLiteral("sizeBytes"), heicContents.size());
    heicEntry.insert(QStringLiteral("sha256"), sha256Of(heicContents));
    heicEntry.insert(
            QStringLiteral("provenance"),
            QStringLiteral("Generated once with libheif 1.23.1 `heif-enc --cut-tiles 64 -q "
                           "90` from a synthetic 128x128 four-colour PNG owned by the pimio "
                           "project; no third-party media is included."));
    heicEntry.insert(
            QStringLiteral("covers"),
            QStringLiteral("A real 2x2 tiled HEIC image for complete thumbnail and detail-view "
                           "decoding."));
    heicEntry.insert(
            QStringLiteral("notes"),
            QStringLiteral("The four 64x64 tiles are solid red, green, blue, and yellow. A video "
                           "decoder that exposes only one HEVC tile produces a cropped single-"
                           "colour image; the HEIF image decoder must compose all four."));
    entries->append(heicEntry);
    return true;
}

} // namespace pimio::fixtures
