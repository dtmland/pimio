#pragma once

#include "pimio/core/types.h"

namespace pimio::tests::browser_support {

inline core::MediaRecord makeRecord(const QString &id, qint64 captureMSecs,
                                    const QString &digest = QString())
{
    core::MediaRecord record;
    record.id = core::MediaId(id);
    record.fingerprint = core::ContentFingerprint(QStringLiteral("sha256"),
                                                   digest.isEmpty() ? id : digest);
    record.identity.absolutePath = QStringLiteral("/library/%1.jpg").arg(id);
    record.identity.volumeId = QStringLiteral("vol-1");
    record.identity.fileId = id;
    record.identity.sizeBytes = 4096;
    record.identity.lastModified = QDateTime::fromMSecsSinceEpoch(captureMSecs, Qt::UTC);

    record.metadata.kind = core::MediaKind::Image;
    record.metadata.fileName = id + QStringLiteral(".jpg");
    record.metadata.folderPath = QStringLiteral("/library");
    record.metadata.captureTime = core::CaptureTime::fromOffset(
        QDateTime::fromMSecsSinceEpoch(captureMSecs, Qt::UTC), 0);
    record.metadata.captureTimeOrigin = core::MetadataOrigin::Embedded;
    record.metadata.cameraMake = QStringLiteral("TestCam");
    record.metadata.cameraModel = QStringLiteral("Model X");
    record.metadata.lensModel = QStringLiteral("Lens 1");
    record.metadata.pixelWidth = 1920;
    record.metadata.pixelHeight = 1080;
    record.metadata.rotationDegrees = 0;
    record.metadata.durationMs = 0;
    record.metadata.hasAudio = false;
    record.metadata.rating = 0;
    record.metadata.caption = QStringLiteral("");
    return record;
}

} // namespace pimio::tests::browser_support
