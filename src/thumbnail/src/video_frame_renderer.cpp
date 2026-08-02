#include "pimio/thumbnail/video_frame_renderer.h"

#include "pimio/core/error.h"

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QMediaPlayer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace pimio::thumbnail {

namespace {

/// Longest time to wait for a frame before treating the file as broken.
///
/// A real video that Qt Multimedia can open produces a frame in well under a
/// second even on a slow machine; ten seconds is generous headroom for a
/// loaded CI runner while still bounding a stuck decode.
constexpr int kDecodeTimeoutMs = 10000;

} // namespace

core::MediaResult VideoFrameRenderer::render(const core::MediaRequest &request,
                                             core::Error *error) const
{
    if (request.absolutePath.isEmpty()) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("MediaRequest.absolutePath is empty"));
        }
        return {};
    }

    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);

    const qint64 targetPositionMs = qMax<qint64>(0, request.positionMs);

    QEventLoop loop;
    QImage frame;
    bool seeked = targetPositionMs == 0;
    core::ErrorCode failureCode = core::ErrorCode::CorruptData;
    QString failureMessage;
    bool failed = false;

    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                     [&](const QVideoFrame &videoFrame) {
        if (!seeked || !videoFrame.isValid() || !frame.isNull()) {
            return;
        }
        frame = videoFrame.toImage();
        if (!frame.isNull()) {
            loop.quit();
        }
    });

    QObject::connect(&player, &QMediaPlayer::positionChanged, &loop, [&](qint64 position) {
        if (!seeked && position >= targetPositionMs) {
            seeked = true;
        }
    });

    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop,
                     [&](QMediaPlayer::MediaStatus status) {
        switch (status) {
        case QMediaPlayer::InvalidMedia:
        case QMediaPlayer::NoMedia:
            failed = true;
            failureCode = core::ErrorCode::UnsupportedMedia;
            failureMessage = QStringLiteral("Unsupported or unreadable video: %1")
                                     .arg(request.absolutePath);
            loop.quit();
            break;
        case QMediaPlayer::LoadedMedia:
            if (targetPositionMs > 0) {
                player.setPosition(targetPositionMs);
            }
            break;
        default:
            break;
        }
    });

    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                     [&](QMediaPlayer::Error, const QString &message) {
        failed = true;
        failureCode = core::ErrorCode::CorruptData;
        failureMessage =
                QStringLiteral("Failed to decode video: %1 — %2").arg(request.absolutePath, message);
        loop.quit();
    });

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    timeoutTimer.start(kDecodeTimeoutMs);

    player.setSource(QUrl::fromLocalFile(request.absolutePath));
    player.play();
    loop.exec();

    player.stop();
    player.setVideoSink(nullptr);

    if (timedOut && frame.isNull()) {
        if (error) {
            *error = core::Error(core::ErrorCode::Timeout,
                                 QStringLiteral("Timed out decoding video: %1")
                                         .arg(request.absolutePath));
        }
        return {};
    }

    if ((failed || frame.isNull())) {
        if (error) {
            *error = core::Error(failed ? failureCode : core::ErrorCode::CorruptData,
                                 failed ? failureMessage
                                        : QStringLiteral("No video frame decoded: %1")
                                                  .arg(request.absolutePath));
        }
        return {};
    }

    if (request.targetSize.isValid()) {
        frame = frame.scaled(request.targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!frame.save(&buffer, "jpeg", 85)) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("Failed to encode video frame as JPEG"));
        }
        return {};
    }

    core::MediaResult result;
    result.bytes = bytes;
    result.format = QStringLiteral("jpeg");
    result.actualSize = frame.size();
    return result;
}

} // namespace pimio::thumbnail
