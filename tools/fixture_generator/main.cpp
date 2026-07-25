#include "exif_builder.h"

#include <QBuffer>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QtEndian>

#include <cstdio>

using namespace pimio::fixtures;

namespace {

struct Fixture
{
    QString path;
    QByteArray contents;
    QString covers;
    QString notes;
};

QImage gradientImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixel(x, y,
                           qRgb(x * 255 / std::max(1, width - 1),
                                y * 255 / std::max(1, height - 1), 128));
        }
    }
    return image;
}

QByteArray encode(const QImage &image, const char *format, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format, quality);
    return bytes;
}

QByteArray baseJpeg()
{
    return encode(gradientImage(32, 24), "JPEG", 80);
}

QByteArray jpegWith(const ExifSpec &spec)
{
    return injectExif(baseJpeg(), buildExifApp1(spec));
}

void appendU32Be(QByteArray &target, quint32 value)
{
    char buffer[4];
    qToBigEndian(value, buffer);
    target.append(buffer, 4);
}

QByteArray box(const char *type, const QByteArray &payload)
{
    QByteArray result;
    appendU32Be(result, static_cast<quint32>(payload.size() + 8));
    result.append(type, 4);
    result.append(payload);
    return result;
}

/// A structurally valid ISO base media file: a real ftyp box and a moov box
/// with a movie header. It is deliberately not decodable; real clips are
/// generated in the video increment where a decoder is available.
QByteArray structuralMp4()
{
    QByteArray ftypPayload;
    ftypPayload.append("isom", 4);
    appendU32Be(ftypPayload, 512);
    ftypPayload.append("isomiso2mp41", 12);

    QByteArray mvhdPayload;
    appendU32Be(mvhdPayload, 0);          // version and flags
    appendU32Be(mvhdPayload, 0);          // creation time
    appendU32Be(mvhdPayload, 0);          // modification time
    appendU32Be(mvhdPayload, 1000);       // timescale
    appendU32Be(mvhdPayload, 2000);       // duration: two seconds
    appendU32Be(mvhdPayload, 0x00010000); // rate
    appendU32Be(mvhdPayload, 0x01000000); // volume and reserved
    appendU32Be(mvhdPayload, 0);
    appendU32Be(mvhdPayload, 0);
    const quint32 identityMatrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    for (quint32 value : identityMatrix) {
        appendU32Be(mvhdPayload, value);
    }
    for (int i = 0; i < 6; ++i) {
        appendU32Be(mvhdPayload, 0);
    }
    appendU32Be(mvhdPayload, 2); // next track id

    QByteArray result;
    result.append(box("ftyp", ftypPayload));
    result.append(box("moov", box("mvhd", mvhdPayload)));
    return result;
}

/// A synthetic RAW-like container holding a real JPEG preview at a recorded
/// offset. It exercises embedded-preview extraction without depending on a RAW
/// decoder or on a camera vendor's proprietary sample file.
QByteArray simulatedRaw()
{
    const QByteArray preview = encode(gradientImage(64, 48), "JPEG", 70);

    QByteArray header;
    header.append("PIMRAW01", 8);
    QByteArray fixed;
    appendU32Be(fixed, 32); // preview offset
    appendU32Be(fixed, static_cast<quint32>(preview.size()));
    appendU32Be(fixed, 4032); // full-resolution width
    appendU32Be(fixed, 3024); // full-resolution height
    header.append(fixed);
    header.append(32 - header.size(), '\0');

    QByteArray result = header;
    result.append(preview);
    return result;
}

QByteArray garbageExifJpeg()
{
    QByteArray app1;
    app1.append(static_cast<char>(0xFF));
    app1.append(static_cast<char>(0xE1));
    app1.append(static_cast<char>(0x00));
    app1.append(static_cast<char>(0x10));
    app1.append("Exif\0\0", 6);
    app1.append("NOT-A-TIFF", 8); // deliberately not a TIFF header
    return injectExif(baseJpeg(), app1);
}

QByteArray xmpSidecar(const QString &captureTime, const QString &offset, const QString &title)
{
    return QStringLiteral(
                   "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                   "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                   " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                   "  <rdf:Description rdf:about=\"\"\n"
                   "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
                   "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
                   "    xmp:CreateDate=\"%1%2\">\n"
                   "   <dc:title>\n"
                   "    <rdf:Alt>\n"
                   "     <rdf:li xml:lang=\"x-default\">%3</rdf:li>\n"
                   "    </rdf:Alt>\n"
                   "   </dc:title>\n"
                   "  </rdf:Description>\n"
                   " </rdf:RDF>\n"
                   "</x:xmpmeta>\n"
                   "<?xpacket end=\"w\"?>\n")
            .arg(captureTime, offset, title)
            .toUtf8();
}

QList<Fixture> buildFixtures()
{
    QList<Fixture> fixtures;

    ExifSpec withOffset;
    withOffset.make = QStringLiteral("pimio");
    withOffset.model = QStringLiteral("Fixture Camera");
    withOffset.dateTimeOriginal = QStringLiteral("2019:05:04 13:45:12");
    withOffset.offsetTimeOriginal = QStringLiteral("+02:00");
    withOffset.hasGps = true;
    withOffset.latitude = 48.8584;
    withOffset.longitude = 2.2945;
    fixtures.append({QStringLiteral("images/jpeg-exif-offset.jpg"), jpegWith(withOffset),
                     QStringLiteral("Capture time with a known UTC offset, camera fields, GPS."),
                     QString()});

    ExifSpec withoutOffset = withOffset;
    withoutOffset.offsetTimeOriginal.clear();
    withoutOffset.hasGps = false;
    fixtures.append({QStringLiteral("images/jpeg-exif-no-offset.jpg"), jpegWith(withoutOffset),
                     QStringLiteral("Capture time with no zone information at all."),
                     QStringLiteral("The offset must stay unknown rather than defaulting to UTC.")});

    ExifSpec rotated = withoutOffset;
    rotated.orientation = 6;
    fixtures.append({QStringLiteral("images/jpeg-exif-orientation-6.jpg"), jpegWith(rotated),
                     QStringLiteral("EXIF orientation 6, a 90 degree clockwise display rotation."),
                     QString()});

    ExifSpec dstGap = withoutOffset;
    dstGap.dateTimeOriginal = QStringLiteral("2019:03:31 01:30:00");
    fixtures.append({QStringLiteral("images/jpeg-exif-dst-gap.jpg"), jpegWith(dstGap),
                     QStringLiteral("A local time that does not exist in Europe/London on that day."),
                     QStringLiteral("Time zone inference must report this as ambiguous.")});

    ExifSpec dstFold = withoutOffset;
    dstFold.dateTimeOriginal = QStringLiteral("2019:10:27 01:30:00");
    fixtures.append({QStringLiteral("images/jpeg-exif-dst-fold.jpg"), jpegWith(dstFold),
                     QStringLiteral("A local time that occurs twice in Europe/London on that day."),
                     QString()});

    ExifSpec leapDay = withOffset;
    leapDay.dateTimeOriginal = QStringLiteral("2020:02:29 23:59:59");
    leapDay.offsetTimeOriginal = QStringLiteral("+13:45");
    leapDay.hasGps = false;
    fixtures.append({QStringLiteral("images/jpeg-exif-leap-day.jpg"), jpegWith(leapDay),
                     QStringLiteral("Leap day with an unusual quarter-hour offset."), QString()});

    fixtures.append({QStringLiteral("images/jpeg-no-exif.jpg"), baseJpeg(),
                     QStringLiteral("A JPEG with no EXIF block."),
                     QStringLiteral("Capture time must fall back to the filesystem visibly.")});

    fixtures.append({QStringLiteral("images/png-solid.png"),
                     encode(gradientImage(16, 16), "PNG", 100),
                     QStringLiteral("A PNG, which carries no EXIF capture time."), QString()});

    fixtures.append({QStringLiteral("raw/simulated-raw-with-preview.pimraw"), simulatedRaw(),
                     QStringLiteral("Embedded-preview extraction from a RAW-like container."),
                     QStringLiteral("Synthetic format owned by pimio. It is not a camera RAW "
                                    "file and must not be treated as a decoder test.")});

    fixtures.append({QStringLiteral("video/structural.mp4"), structuralMp4(),
                     QStringLiteral("ISO base media container parsing: ftyp, moov, mvhd."),
                     QStringLiteral("Structurally valid but deliberately not decodable. Real "
                                    "clips are generated in the video increment.")});

    fixtures.append({QStringLiteral("malformed/truncated.jpg"), baseJpeg().left(64),
                     QStringLiteral("A JPEG truncated mid-stream."),
                     QStringLiteral("Must produce a visible error record, not a crash.")});

    fixtures.append({QStringLiteral("malformed/garbage-exif.jpg"), garbageExifJpeg(),
                     QStringLiteral("A readable JPEG whose EXIF block is corrupt."),
                     QStringLiteral("The image must still be usable; the metadata failure is a "
                                    "warning, not a fatal error.")});

    fixtures.append({QStringLiteral("malformed/empty.jpg"), QByteArray(),
                     QStringLiteral("A zero-byte file with an image extension."), QString()});

    fixtures.append({QStringLiteral("malformed/text-with-jpg-extension.jpg"),
                     QByteArrayLiteral("This is not an image.\n"),
                     QStringLiteral("Content that contradicts its extension."),
                     QStringLiteral("Type detection must not rely on the extension alone.")});

    fixtures.append({QStringLiteral("sidecars/jpeg-exif-offset.xmp"),
                     xmpSidecar(QStringLiteral("2019-05-04T11:45:12"), QStringLiteral("Z"),
                                QStringLiteral("Sidecar title")),
                     QStringLiteral("A sidecar whose capture time disagrees with the embedded "
                                    "EXIF of images/jpeg-exif-offset.jpg."),
                     QStringLiteral("Precedence must be explicit and the disagreement must be "
                                    "recorded as a conflict.")});

    fixtures.append({QStringLiteral("sidecars/orphan.xmp"),
                     xmpSidecar(QStringLiteral("2021-01-01T00:00:00"), QStringLiteral("+00:00"),
                                QStringLiteral("Orphan sidecar")),
                     QStringLiteral("A sidecar with no corresponding media file."), QString()});

    return fixtures;
}

QString sha256Of(const QByteArray &contents)
{
    return QString::fromLatin1(
            QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
}

} // namespace

int main(int argc, char *argv[])
{
    // QImage encoding needs the GUI module but no display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("pimio-fixture-generator"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
            QStringLiteral("Regenerates the pimio test fixture corpus and its manifest."));
    parser.addHelpOption();
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("Fixture directory to write."),
                                    QStringLiteral("directory"));
    parser.addOption(outputOption);
    parser.process(application);

    if (!parser.isSet(outputOption)) {
        std::fprintf(stderr, "An output directory is required.\n");
        return 2;
    }

    const QDir outputDir(parser.value(outputOption));
    const QList<Fixture> fixtures = buildFixtures();

    QJsonArray entries;
    for (const Fixture &fixture : fixtures) {
        const QString absolutePath = outputDir.absoluteFilePath(fixture.path);
        if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
            std::fprintf(stderr, "Cannot create directory for %s\n",
                         qPrintable(fixture.path));
            return 1;
        }

        QFile file(absolutePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "Cannot write %s\n", qPrintable(absolutePath));
            return 1;
        }
        if (!fixture.contents.isEmpty()
            && file.write(fixture.contents) != fixture.contents.size()) {
            std::fprintf(stderr, "Short write for %s\n", qPrintable(absolutePath));
            return 1;
        }
        file.close();

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), fixture.path);
        entry.insert(QStringLiteral("sizeBytes"), fixture.contents.size());
        entry.insert(QStringLiteral("sha256"), sha256Of(fixture.contents));
        entry.insert(QStringLiteral("provenance"),
                     QStringLiteral("Generated by tools/fixture_generator. Owned by the pimio "
                                    "project; no third-party media is included."));
        entry.insert(QStringLiteral("covers"), fixture.covers);
        if (!fixture.notes.isEmpty()) {
            entry.insert(QStringLiteral("notes"), fixture.notes);
        }
        entries.append(entry);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("generator"), QStringLiteral("tools/fixture_generator"));
    manifest.insert(QStringLiteral("generatedWithQtVersion"),
                    QString::fromLatin1(qVersion()));
    manifest.insert(QStringLiteral("fixtures"), entries);

    QFile manifestFile(outputDir.absoluteFilePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "Cannot write the manifest.\n");
        return 1;
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();

    std::printf("Wrote %lld fixtures to %s\n", static_cast<long long>(fixtures.size()),
                qPrintable(outputDir.absolutePath()));
    return 0;
}
