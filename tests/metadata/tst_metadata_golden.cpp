#include "pimio/metadata/builtin_metadata_reader.h"

#include "pimio/core/error.h"
#include "pimio/core/metadata.h"
#include "pimio/scan/scanner.h"
#include "pimio/testing/fake_clock.h"
#include "pimio/testing/memory_durable_store.h"
#include "pimio/testing/memory_file_system.h"
#include "pimio/testing/qtest_printers.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

#ifndef PIMIO_FIXTURES_DIR
#error "PIMIO_FIXTURES_DIR must be defined by the build system"
#endif

using namespace pimio;
using namespace pimio::metadata;

namespace {

QString fixture(const QString &relativePath)
{
    return QDir(QStringLiteral(PIMIO_FIXTURES_DIR)).absoluteFilePath(relativePath);
}

QByteArray fixtureBytes(const QString &relativePath)
{
    QFile file(fixture(relativePath));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

/// Reads a fixture and fails the test with a readable message when the reader
/// rejected a file the test expects to be readable.
std::optional<core::MetadataReadResult> readFixture(const BuiltinMetadataReader &reader,
                                                    const QString &relativePath)
{
    core::Error error;
    auto result = reader.read(fixture(relativePath), &error);
    if (!result.has_value()) {
        qWarning("Reading %s failed: %s", qPrintable(relativePath),
                 qPrintable(error.message()));
    }
    return result;
}

const core::MetadataConflict *conflictFor(const core::MediaMetadata &metadata,
                                          const QString &field)
{
    for (const core::MetadataConflict &conflict : metadata.conflicts) {
        if (conflict.field == field) {
            return &conflict;
        }
    }
    return nullptr;
}

} // namespace

/// Golden tests for the built-in metadata reader.
///
/// Every case is driven by a committed fixture, so a change in what pimio
/// reads out of a real file is a diff in this test rather than a silent change
/// in behaviour. The fixtures themselves are guarded by fixtures.manifest.
class TestMetadataGolden : public QObject
{
    Q_OBJECT

private slots:
    // ---- Capture time ----

    void captureTimeWithKnownOffsetIsExact();
    void captureTimeWithoutOffsetKeepsTheZoneUnknown();
    void captureTimeSurvivesALeapDayAndAQuarterHourOffset();
    void missingCaptureTimeFallsBackToTheFilesystemVisibly();
    void sidecarOutranksEmbeddedAndRecordsTheDisagreement();
    void sidecarAgreementRecordsNoConflict();
    void sidecarSuppliesACaptionAndUnicodeIsPreserved();

    // ---- Image facts ----

    void cameraFieldsAreRead();
    void pixelDimensionsComeFromTheFrameHeader();
    void exifOrientationBecomesADisplayRotation();
    void gpsCoordinatesAreRead();
    void pngIsReadableAndCarriesNoCaptureTime();

    // ---- Video facts ----

    void videoDurationIsReadFromTheContainer();
    void videoAudioTrackPresenceIsReported();
    void videoWithoutTracksReportsNoAudio();

    // ---- HEIF-family still images (AVIF, HEIC) ----

    void avifIsClassifiedAsAnImageNotAVideo();
    void heicIsClassifiedAsAnImageNotAVideo();
    void heifImageDimensionsAreReadFromTheContainer();
    void heifImageCarriesNoDurationOrAudio();

    // ---- Failure policy ----

    void damagedExifIsAWarningAndTheImageIsStillRead();
    void truncatedImageIsACorruptDataError();
    void emptyFileIsUnsupportedMedia();
    void contentContradictingItsExtensionIsUnsupportedMedia();
    void unknownContainerIsUnsupportedMedia();
    void missingFileIsNotFound();
    void unsupportedMediaDoesNotBlockAScan();

    // ---- Boundaries ----

    void readingThroughAnInjectedFilesystemMatchesTheDisk();
};

// ---- Capture time ----

void TestMetadataGolden::captureTimeWithKnownOffsetIsExact()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-exif-offset.jpg"));
    QVERIFY(result.has_value());

    const core::CaptureTime &captureTime = result->metadata.captureTime;
    QVERIFY(captureTime.isValid());
    QCOMPARE(captureTime.wallClock().date(), QDate(2019, 5, 4));
    QCOMPARE(captureTime.wallClock().time(), QTime(13, 45, 12));
    QVERIFY(captureTime.hasKnownOffset());
    QCOMPARE(captureTime.utcOffsetSeconds().value(), 2 * 3600);
    PIMIO_COMPARE_ENUM(result->metadata.captureTimeOrigin, core::MetadataOrigin::Embedded);

    const auto utc = captureTime.toUtc();
    QVERIFY(utc.has_value());
    QCOMPARE(utc->time(), QTime(11, 45, 12));
}

void TestMetadataGolden::captureTimeWithoutOffsetKeepsTheZoneUnknown()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-exif-no-offset.jpg"));
    QVERIFY(result.has_value());

    const core::CaptureTime &captureTime = result->metadata.captureTime;
    QVERIFY(captureTime.isValid());
    QCOMPARE(captureTime.wallClock().time(), QTime(13, 45, 12));
    // The file says nothing about a zone, so neither does pimio.
    QVERIFY(!captureTime.hasKnownOffset());
    QVERIFY(!captureTime.toUtc().has_value());
    PIMIO_COMPARE_ENUM(result->metadata.captureTimeOrigin, core::MetadataOrigin::Embedded);
}

void TestMetadataGolden::captureTimeSurvivesALeapDayAndAQuarterHourOffset()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-exif-leap-day.jpg"));
    QVERIFY(result.has_value());

    const core::CaptureTime &captureTime = result->metadata.captureTime;
    QCOMPARE(captureTime.wallClock().date(), QDate(2020, 2, 29));
    QCOMPARE(captureTime.wallClock().time(), QTime(23, 59, 59));
    QCOMPARE(captureTime.utcOffsetSeconds().value(), 13 * 3600 + 45 * 60);
}

void TestMetadataGolden::missingCaptureTimeFallsBackToTheFilesystemVisibly()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-no-exif.jpg"));
    QVERIFY(result.has_value());

    QVERIFY(result->metadata.captureTime.isValid());
    // The origin is what makes the fallback honest: the UI can say where the
    // date came from instead of presenting a file date as a capture date.
    PIMIO_COMPARE_ENUM(result->metadata.captureTimeOrigin, core::MetadataOrigin::FileSystem);
}

void TestMetadataGolden::sidecarOutranksEmbeddedAndRecordsTheDisagreement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("images/jpeg-exif-offset.jpg")), imagePath));
    QVERIFY(QFile::copy(fixture(QStringLiteral("sidecars/jpeg-exif-offset.xmp")),
                        directory.filePath(QStringLiteral("photo.xmp"))));

    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result = reader.read(imagePath, &error);
    QVERIFY(result.has_value());
    QVERIFY(result->usedSidecar);

    // The sidecar records the same instant as UTC; the embedded block records
    // it as a local time with a +02:00 offset.
    const core::CaptureTime &captureTime = result->metadata.captureTime;
    QCOMPARE(captureTime.wallClock().time(), QTime(11, 45, 12));
    QCOMPARE(captureTime.utcOffsetSeconds().value(), 0);
    PIMIO_COMPARE_ENUM(result->metadata.captureTimeOrigin, core::MetadataOrigin::Sidecar);

    const core::MetadataConflict *conflict =
            conflictFor(result->metadata, QStringLiteral("captureTime"));
    QVERIFY2(conflict != nullptr, "The overridden camera value must stay visible.");
    PIMIO_COMPARE_ENUM(conflict->preferredOrigin, core::MetadataOrigin::Sidecar);
    PIMIO_COMPARE_ENUM(conflict->conflictingOrigin, core::MetadataOrigin::Embedded);
    QVERIFY(conflict->preferredValue.contains(QStringLiteral("11:45:12")));
    QVERIFY(conflict->conflictingValue.contains(QStringLiteral("13:45:12")));
}

void TestMetadataGolden::sidecarAgreementRecordsNoConflict()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("images/jpeg-exif-offset.jpg")), imagePath));

    // The same instant the embedded block records, written the way a sidecar
    // writes it.
    QFile sidecar(directory.filePath(QStringLiteral("photo.xmp")));
    QVERIFY(sidecar.open(QIODevice::WriteOnly));
    sidecar.write(
            "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
            " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            "  <rdf:Description rdf:about=\"\"\n"
            "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
            "    xmp:CreateDate=\"2019-05-04T13:45:12+02:00\"/>\n"
            " </rdf:RDF>\n"
            "</x:xmpmeta>\n");
    sidecar.close();

    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result = reader.read(imagePath, &error);
    QVERIFY(result.has_value());
    QVERIFY(result->usedSidecar);
    QVERIFY(conflictFor(result->metadata, QStringLiteral("captureTime")) == nullptr);
    QCOMPARE(result->metadata.captureTime.utcOffsetSeconds().value(), 2 * 3600);
}

void TestMetadataGolden::sidecarSuppliesACaptionAndUnicodeIsPreserved()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("images/jpeg-no-exif.jpg")), imagePath));

    QFile sidecar(directory.filePath(QStringLiteral("photo.jpg.xmp")));
    QVERIFY(sidecar.open(QIODevice::WriteOnly));
    sidecar.write(QString::fromUtf8(
                          "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                          "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                          " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                          "  <rdf:Description rdf:about=\"\"\n"
                          "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
                          "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
                          "    xmp:Rating=\"4\">\n"
                          "   <dc:title>\n"
                          "    <rdf:Alt>\n"
                          "     <rdf:li xml:lang=\"x-default\">東京タワー</rdf:li>\n"
                          "    </rdf:Alt>\n"
                          "   </dc:title>\n"
                          "   <dc:subject>\n"
                          "    <rdf:Bag>\n"
                          "     <rdf:li>travel</rdf:li>\n"
                          "     <rdf:li>日本</rdf:li>\n"
                          "    </rdf:Bag>\n"
                          "   </dc:subject>\n"
                          "  </rdf:Description>\n"
                          " </rdf:RDF>\n"
                          "</x:xmpmeta>\n")
                          .toUtf8());
    sidecar.close();

    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result = reader.read(imagePath, &error);
    QVERIFY(result.has_value());
    QVERIFY(result->usedSidecar);
    QCOMPARE(result->metadata.caption, QString::fromUtf8("東京タワー"));
    QCOMPARE(result->metadata.rating, 4);
    QCOMPARE(result->metadata.tags,
             QStringList({QStringLiteral("travel"), QString::fromUtf8("日本")}));
}

// ---- Image facts ----

void TestMetadataGolden::cameraFieldsAreRead()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-exif-offset.jpg"));
    QVERIFY(result.has_value());
    QCOMPARE(result->metadata.cameraMake, QStringLiteral("pimio"));
    QCOMPARE(result->metadata.cameraModel, QStringLiteral("Fixture Camera"));
    PIMIO_COMPARE_ENUM(result->metadata.kind, core::MediaKind::Image);
    QCOMPARE(result->metadata.fileName, QStringLiteral("jpeg-exif-offset.jpg"));
}

void TestMetadataGolden::pixelDimensionsComeFromTheFrameHeader()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-exif-offset.jpg"));
    QVERIFY(result.has_value());
    QCOMPARE(result->metadata.pixelWidth, 32);
    QCOMPARE(result->metadata.pixelHeight, 24);
}

void TestMetadataGolden::exifOrientationBecomesADisplayRotation()
{
    const BuiltinMetadataReader reader;
    const auto rotated = readFixture(reader, QStringLiteral("images/jpeg-exif-orientation-6.jpg"));
    QVERIFY(rotated.has_value());
    QCOMPARE(rotated->metadata.rotationDegrees, 90);

    const auto upright = readFixture(reader, QStringLiteral("images/jpeg-exif-offset.jpg"));
    QVERIFY(upright.has_value());
    QCOMPARE(upright->metadata.rotationDegrees, 0);
}

void TestMetadataGolden::gpsCoordinatesAreRead()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/jpeg-exif-offset.jpg"));
    QVERIFY(result.has_value());
    QVERIFY(result->metadata.location.has_value());
    // The fixture stores seconds as thousandths, so the value round-trips to
    // roughly a millisecond of arc.
    QVERIFY(qAbs(result->metadata.location->latitude() - 48.8584) < 0.0005);
    QVERIFY(qAbs(result->metadata.location->longitude() - 2.2945) < 0.0005);

    const auto withoutGps = readFixture(reader, QStringLiteral("images/jpeg-exif-no-offset.jpg"));
    QVERIFY(withoutGps.has_value());
    QVERIFY(!withoutGps->metadata.location.has_value());
}

void TestMetadataGolden::pngIsReadableAndCarriesNoCaptureTime()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/png-solid.png"));
    QVERIFY(result.has_value());
    PIMIO_COMPARE_ENUM(result->metadata.kind, core::MediaKind::Image);
    QCOMPARE(result->metadata.pixelWidth, 16);
    QCOMPARE(result->metadata.pixelHeight, 16);
    PIMIO_COMPARE_ENUM(result->metadata.captureTimeOrigin, core::MetadataOrigin::FileSystem);
}

// ---- Video facts ----

void TestMetadataGolden::videoDurationIsReadFromTheContainer()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("video/audio-video.mp4"));
    QVERIFY(result.has_value());
    PIMIO_COMPARE_ENUM(result->metadata.kind, core::MediaKind::Video);
    QCOMPARE(result->metadata.durationMs, 5000);
    QCOMPARE(result->metadata.pixelWidth, 1920);
    QCOMPARE(result->metadata.pixelHeight, 1080);
}

void TestMetadataGolden::videoAudioTrackPresenceIsReported()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("video/audio-video.mp4"));
    QVERIFY(result.has_value());
    QVERIFY(result->metadata.hasAudio);
}

void TestMetadataGolden::videoWithoutTracksReportsNoAudio()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("video/structural.mp4"));
    QVERIFY(result.has_value());
    QCOMPARE(result->metadata.durationMs, 2000);
    QVERIFY(!result->metadata.hasAudio);
}

// ---- HEIF-family still images (AVIF, HEIC) ----

void TestMetadataGolden::avifIsClassifiedAsAnImageNotAVideo()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/avif-still.avif"));
    QVERIFY(result.has_value());
    // AVIF is an AV1 still picture in an ISO base media container. It must be an
    // image, not a video, or the app tries to "play" a photo.
    PIMIO_COMPARE_ENUM(result->metadata.kind, core::MediaKind::Image);
}

void TestMetadataGolden::heicIsClassifiedAsAnImageNotAVideo()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/heic-still.heic"));
    QVERIFY(result.has_value());
    PIMIO_COMPARE_ENUM(result->metadata.kind, core::MediaKind::Image);
}

void TestMetadataGolden::heifImageDimensionsAreReadFromTheContainer()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/heic-still.heic"));
    QVERIFY(result.has_value());
    QCOMPARE(result->metadata.pixelWidth, 4032);
    QCOMPARE(result->metadata.pixelHeight, 3024);
}

void TestMetadataGolden::heifImageCarriesNoDurationOrAudio()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("images/avif-still.avif"));
    QVERIFY(result.has_value());
    // A still image has no timeline; claiming a duration or audio track would be
    // a fact the file does not support, and it must not be warned about either.
    QCOMPARE(result->metadata.durationMs, 0);
    QVERIFY(!result->metadata.hasAudio);
    QVERIFY(result->warnings.isEmpty());
}

// ---- Failure policy ----

void TestMetadataGolden::damagedExifIsAWarningAndTheImageIsStillRead()
{
    const BuiltinMetadataReader reader;
    const auto result = readFixture(reader, QStringLiteral("malformed/garbage-exif.jpg"));
    QVERIFY2(result.has_value(), "A damaged EXIF block must not lose the photo.");
    QVERIFY(!result->warnings.isEmpty());
    QCOMPARE(result->metadata.pixelWidth, 32);
    QCOMPARE(result->metadata.pixelHeight, 24);
    PIMIO_COMPARE_ENUM(result->metadata.captureTimeOrigin, core::MetadataOrigin::FileSystem);
}

void TestMetadataGolden::truncatedImageIsACorruptDataError()
{
    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result = reader.read(fixture(QStringLiteral("malformed/truncated.jpg")), &error);
    QVERIFY(!result.has_value());
    PIMIO_COMPARE_ENUM(error.code(), core::ErrorCode::CorruptData);
    QVERIFY(!error.message().isEmpty());
}

void TestMetadataGolden::emptyFileIsUnsupportedMedia()
{
    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result = reader.read(fixture(QStringLiteral("malformed/empty.jpg")), &error);
    QVERIFY(!result.has_value());
    PIMIO_COMPARE_ENUM(error.code(), core::ErrorCode::UnsupportedMedia);
}

void TestMetadataGolden::contentContradictingItsExtensionIsUnsupportedMedia()
{
    const BuiltinMetadataReader reader;
    const QString path = fixture(QStringLiteral("malformed/text-with-jpg-extension.jpg"));
    // The reader still claims the file, so that the scan gets a record of it
    // instead of the file quietly disappearing.
    QVERIFY(reader.supports(path));

    core::Error error;
    const auto result = reader.read(path, &error);
    QVERIFY(!result.has_value());
    PIMIO_COMPARE_ENUM(error.code(), core::ErrorCode::UnsupportedMedia);
}

void TestMetadataGolden::unknownContainerIsUnsupportedMedia()
{
    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result =
            reader.read(fixture(QStringLiteral("raw/simulated-raw-with-preview.pimraw")), &error);
    QVERIFY(!result.has_value());
    PIMIO_COMPARE_ENUM(error.code(), core::ErrorCode::UnsupportedMedia);
}

void TestMetadataGolden::missingFileIsNotFound()
{
    const BuiltinMetadataReader reader;
    core::Error error;
    const auto result = reader.read(fixture(QStringLiteral("images/does-not-exist.jpg")), &error);
    QVERIFY(!result.has_value());
    PIMIO_COMPARE_ENUM(error.code(), core::ErrorCode::NotFound);
}

void TestMetadataGolden::unsupportedMediaDoesNotBlockAScan()
{
    testing::MemoryFileSystem fileSystem;
    fileSystem.addDirectory(QStringLiteral("/lib"));
    fileSystem.addFile(QStringLiteral("/lib/photo.jpg"),
                       fixtureBytes(QStringLiteral("images/jpeg-exif-offset.jpg")));
    fileSystem.addFile(QStringLiteral("/lib/not-really.jpg"),
                       fixtureBytes(QStringLiteral("malformed/text-with-jpg-extension.jpg")));

    testing::FakeClock clock(QDateTime(QDate(2024, 1, 1), QTime(0, 0, 0), Qt::UTC));
    testing::MemoryDurableStore store(clock);
    BuiltinMetadataReader reader(&fileSystem);

    scan::Scanner scanner(&fileSystem, &reader, &store);
    std::atomic<bool> cancelled{false};
    scan::Scanner::Result result;
    const core::Error error = scanner.scan({QStringLiteral("/lib")}, cancelled, &result);

    QVERIFY(!error.isError());
    // Both files are indexed; the unreadable one is reported, not dropped.
    QCOMPARE(result.added, 2);
    QCOMPARE(result.warnings.size(), 1);
    PIMIO_COMPARE_ENUM(result.warnings.first().code(), core::ErrorCode::UnsupportedMedia);

    core::Error loadError;
    const QList<core::MediaId> ids = store.listIds(&loadError);
    QVERIFY(!loadError.isError());
    QCOMPARE(ids.size(), 2);
}

// ---- Boundaries ----

void TestMetadataGolden::readingThroughAnInjectedFilesystemMatchesTheDisk()
{
    const QString relativePath = QStringLiteral("images/jpeg-exif-offset.jpg");

    testing::MemoryFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/lib/photo.jpg"), fixtureBytes(relativePath));
    const BuiltinMetadataReader injected(&fileSystem);
    core::Error injectedError;
    const auto fromMemory = injected.read(QStringLiteral("/lib/photo.jpg"), &injectedError);
    QVERIFY(fromMemory.has_value());

    const BuiltinMetadataReader onDisk;
    const auto fromDisk = readFixture(onDisk, relativePath);
    QVERIFY(fromDisk.has_value());

    QCOMPARE(fromMemory->metadata.captureTime, fromDisk->metadata.captureTime);
    QCOMPARE(fromMemory->metadata.cameraModel, fromDisk->metadata.cameraModel);
    QCOMPARE(fromMemory->metadata.pixelWidth, fromDisk->metadata.pixelWidth);
    QVERIFY(fromMemory->metadata.location == fromDisk->metadata.location);
}

QTEST_MAIN(TestMetadataGolden)
#include "tst_metadata_golden.moc"
