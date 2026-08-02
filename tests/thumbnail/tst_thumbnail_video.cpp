#include "pimio/thumbnail/video_frame_renderer.h"

#include "pimio/thumbnail/composite_renderer.h"
#include "pimio/thumbnail/image_renderer.h"

#include "pimio/core/error.h"
#include "pimio/core/media_request.h"

#include <QDir>
#include <QImage>
#include <QTest>

#ifndef PIMIO_FIXTURES_DIR
#error "PIMIO_FIXTURES_DIR must be defined by the build system"
#endif

using namespace pimio::thumbnail;
using namespace pimio::core;

namespace {

QString fixturePath(const QString &relative)
{
    return QDir(QStringLiteral(PIMIO_FIXTURES_DIR)).absoluteFilePath(relative);
}

} // namespace

/// Covers the video-frame thumbnail path added to close out Increment 6:
/// VideoFrameRenderer decoding a real clip through Qt Multimedia, its
/// failure behaviour on non-decodable and missing input, and CompositeRenderer
/// dispatching between the image and video renderers by content.
class TestThumbnailVideo : public QObject
{
    Q_OBJECT

private slots:
    void decodesFirstFrameOfARealClip();
    void decodesAtARequestedPosition();
    void reportsErrorForAStructurallyValidButUndecodableFile();
    void reportsInternalErrorForAnEmptyPath();
    void compositeDispatchesImagesToTheImageRenderer();
    void compositeDispatchesVideoToTheVideoRenderer();
    void compositeReturnsCorruptImageErrorsWithoutTryingVideo();
};

void TestThumbnailVideo::decodesFirstFrameOfARealClip()
{
    VideoFrameRenderer renderer;

    MediaRequest request;
    request.absolutePath = fixturePath(QStringLiteral("video/decodable-clip.mp4"));
    request.targetSize = QSize(64, 64);

    Error error;
    const MediaResult result = renderer.render(request, &error);

    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QCOMPARE(result.format, QStringLiteral("jpeg"));
    QVERIFY(!result.bytes.isEmpty());
    QVERIFY(result.actualSize.isValid());

    const QImage decoded = QImage::fromData(result.bytes, "jpeg");
    QVERIFY(!decoded.isNull());

    // The source clip is a solid red frame; a JPEG re-encode keeps red the
    // clearly dominant channel even with chroma-subsampling artefacts.
    const QColor center = decoded.pixelColor(decoded.width() / 2, decoded.height() / 2);
    QVERIFY2(center.red() > center.green() + 40 && center.red() > center.blue() + 40,
             qPrintable(QStringLiteral("Unexpected frame colour: %1,%2,%3")
                                .arg(center.red())
                                .arg(center.green())
                                .arg(center.blue())));
}

void TestThumbnailVideo::decodesAtARequestedPosition()
{
    VideoFrameRenderer renderer;

    MediaRequest request;
    request.absolutePath = fixturePath(QStringLiteral("video/decodable-clip.mp4"));
    request.targetSize = QSize(64, 64);
    // Every frame in the fixture is the same solid colour, so seeking part
    // way into the one-second clip exercises the position-seeking code path
    // without making the assertion depend on exact per-frame timing.
    request.positionMs = 400;

    Error error;
    const MediaResult result = renderer.render(request, &error);

    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QVERIFY(!result.bytes.isEmpty());
}

void TestThumbnailVideo::reportsErrorForAStructurallyValidButUndecodableFile()
{
    VideoFrameRenderer renderer;

    MediaRequest request;
    // structural.mp4 is a valid ISO base media file with no samples: Qt
    // Multimedia can identify it as MP4 but finds no playable stream, which
    // must surface as a clear error rather than a hang.
    request.absolutePath = fixturePath(QStringLiteral("video/structural.mp4"));
    request.targetSize = QSize(64, 64);

    Error error;
    const MediaResult result = renderer.render(request, &error);

    QVERIFY(result.bytes.isEmpty());
    QVERIFY(error.isError());
    QVERIFY2(error.code() == ErrorCode::UnsupportedMedia || error.code() == ErrorCode::CorruptData
                    || error.code() == ErrorCode::Timeout,
             qPrintable(toString(error.code())));
}

void TestThumbnailVideo::reportsInternalErrorForAnEmptyPath()
{
    VideoFrameRenderer renderer;

    MediaRequest request;
    request.targetSize = QSize(64, 64);

    Error error;
    const MediaResult result = renderer.render(request, &error);

    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(ErrorCode::Internal));
}

void TestThumbnailVideo::compositeDispatchesImagesToTheImageRenderer()
{
    ImageRenderer imageRenderer;
    VideoFrameRenderer videoRenderer;
    CompositeRenderer composite(&imageRenderer, &videoRenderer);

    MediaRequest request;
    request.absolutePath = fixturePath(QStringLiteral("images/jpeg-no-exif.jpg"));
    request.targetSize = QSize(64, 64);

    Error error;
    const MediaResult result = composite.render(request, &error);

    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QVERIFY(!result.bytes.isEmpty());
}

void TestThumbnailVideo::compositeDispatchesVideoToTheVideoRenderer()
{
    ImageRenderer imageRenderer;
    VideoFrameRenderer videoRenderer;
    CompositeRenderer composite(&imageRenderer, &videoRenderer);

    MediaRequest request;
    request.absolutePath = fixturePath(QStringLiteral("video/decodable-clip.mp4"));
    request.targetSize = QSize(64, 64);

    Error error;
    const MediaResult result = composite.render(request, &error);

    QVERIFY2(!error.isError(), qPrintable(error.message()));
    QVERIFY(!result.bytes.isEmpty());
}

void TestThumbnailVideo::compositeReturnsCorruptImageErrorsWithoutTryingVideo()
{
    ImageRenderer imageRenderer;
    VideoFrameRenderer videoRenderer;
    CompositeRenderer composite(&imageRenderer, &videoRenderer);

    MediaRequest request;
    // Recognised as a JPEG by its signature, but truncated: the image
    // renderer must report CorruptData, and the composite must return that
    // rather than trying (and failing differently in) the video renderer.
    request.absolutePath = fixturePath(QStringLiteral("malformed/truncated.jpg"));
    request.targetSize = QSize(64, 64);

    Error error;
    const MediaResult result = composite.render(request, &error);

    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(static_cast<int>(error.code()), static_cast<int>(ErrorCode::CorruptData));
}

QTEST_MAIN(TestThumbnailVideo)

#include "tst_thumbnail_video.moc"
