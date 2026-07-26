#include "pimio/metadata/builtin_metadata_reader.h"

#include "image_reader.h"
#include "iso_bmff_reader.h"
#include "media_signature.h"
#include "xmp_reader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace pimio::metadata {
namespace {

QString describe(const core::CaptureTime &captureTime)
{
    if (!captureTime.isValid()) {
        return {};
    }
    const QString wallClock = captureTime.wallClock().toString(Qt::ISODate);
    if (const auto offset = captureTime.utcOffsetSeconds()) {
        const int minutes = *offset / 60;
        return QStringLiteral("%1 %2%3:%4")
                .arg(wallClock, minutes < 0 ? QStringLiteral("-") : QStringLiteral("+"))
                .arg(qAbs(minutes) / 60, 2, 10, QLatin1Char('0'))
                .arg(qAbs(minutes) % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1 (zone unknown)").arg(wallClock);
}

/// Applies one field of \a higher over \a lower, recording the disagreement so
/// the user can see that a value was overridden and by what.
template <typename T, typename Describe>
void resolve(const QString &field, const std::optional<T> &lower, core::MetadataOrigin lowerOrigin,
             const std::optional<T> &higher, core::MetadataOrigin higherOrigin,
             std::optional<T> *resolved, core::MetadataOrigin *resolvedOrigin,
             QList<core::MetadataConflict> *conflicts, Describe describe)
{
    if (!higher.has_value()) {
        *resolved = lower;
        if (lower.has_value()) {
            *resolvedOrigin = lowerOrigin;
        }
        return;
    }

    *resolved = higher;
    *resolvedOrigin = higherOrigin;

    if (lower.has_value() && *lower != *higher) {
        core::MetadataConflict conflict;
        conflict.field = field;
        conflict.preferredOrigin = higherOrigin;
        conflict.conflictingOrigin = lowerOrigin;
        conflict.preferredValue = describe(*higher);
        conflict.conflictingValue = describe(*lower);
        conflicts->append(conflict);
    }
}

core::MediaKind kindFor(MediaSignature signature)
{
    switch (signature) {
    case MediaSignature::Jpeg:
    case MediaSignature::Png:
    case MediaSignature::Tiff:
        return core::MediaKind::Image;
    case MediaSignature::IsoBmff:
        return core::MediaKind::Video;
    case MediaSignature::Unknown:
        break;
    }
    return core::MediaKind::Unknown;
}

} // namespace

class BuiltinMetadataReader::Private
{
public:
    core::FileSystem *fileSystem = nullptr;

    bool exists(const QString &path) const
    {
        return fileSystem != nullptr ? fileSystem->exists(path) : QFile::exists(path);
    }

    QByteArray readAll(const QString &path, core::Error *error) const
    {
        if (fileSystem != nullptr) {
            return fileSystem->readAll(path, error);
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            const core::ErrorCode code = file.exists() ? core::ErrorCode::PermissionDenied
                                                       : core::ErrorCode::NotFound;
            *error = core::Error(code,
                                 QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
            return {};
        }
        return file.readAll();
    }

    QDateTime lastModified(const QString &path) const
    {
        if (fileSystem != nullptr) {
            core::Error error;
            const core::FileIdentity identity = fileSystem->identify(path, &error);
            return error.isError() ? QDateTime() : identity.lastModified;
        }
        return QFileInfo(path).lastModified();
    }
};

BuiltinMetadataReader::BuiltinMetadataReader(core::FileSystem *fileSystem)
    : d(std::make_unique<Private>())
{
    d->fileSystem = fileSystem;
}

BuiltinMetadataReader::~BuiltinMetadataReader() = default;

bool BuiltinMetadataReader::supports(const QString &absolutePath) const
{
    core::Error error;
    const QByteArray contents = d->readAll(absolutePath, &error);
    if (error.isError()) {
        return false;
    }
    if (signatureOf(contents.left(kSignatureProbeBytes)) != MediaSignature::Unknown) {
        return true;
    }
    return hasMediaExtension(QFileInfo(absolutePath).fileName());
}

QString BuiltinMetadataReader::sidecarPathFor(const QString &absolutePath) const
{
    const QFileInfo info(absolutePath);
    const QString appended = absolutePath + QStringLiteral(".xmp");
    if (d->exists(appended)) {
        return appended;
    }
    const QString baseName = info.completeBaseName();
    const QString directory = info.path();
    return directory.isEmpty() ? baseName + QStringLiteral(".xmp")
                               : directory + QLatin1Char('/') + baseName + QStringLiteral(".xmp");
}

std::optional<core::MetadataReadResult>
BuiltinMetadataReader::read(const QString &absolutePath, core::Error *error) const
{
    core::Error readError;
    const QByteArray contents = d->readAll(absolutePath, &readError);
    if (readError.isError()) {
        *error = readError.withContext(QJsonObject{{QStringLiteral("path"), absolutePath}});
        return std::nullopt;
    }

    const MediaSignature signature = signatureOf(contents.left(kSignatureProbeBytes));
    if (signature == MediaSignature::Unknown) {
        *error = core::Error(core::ErrorCode::UnsupportedMedia,
                             QStringLiteral("%1 is not a media format pimio can read.")
                                     .arg(QFileInfo(absolutePath).fileName()))
                         .withContext(QJsonObject{{QStringLiteral("path"), absolutePath}});
        return std::nullopt;
    }

    QStringList warnings;
    FieldSet embedded;
    bool parsed = false;
    switch (signature) {
    case MediaSignature::Jpeg:
        parsed = readJpeg(contents, &embedded, &warnings);
        break;
    case MediaSignature::Png:
        parsed = readPng(contents, &embedded, &warnings);
        break;
    case MediaSignature::Tiff:
        parsed = readTiff(contents, &embedded, &warnings);
        break;
    case MediaSignature::IsoBmff:
        parsed = readIsoBmff(contents, &embedded, &warnings);
        break;
    case MediaSignature::Unknown:
        break;
    }

    if (!parsed) {
        // The container was recognized but its structure is broken. That is a
        // different fact from "unknown format", and the user can act on it.
        *error = core::Error(core::ErrorCode::CorruptData,
                             QStringLiteral("%1 is damaged and cannot be read: %2")
                                     .arg(QFileInfo(absolutePath).fileName(),
                                          warnings.isEmpty()
                                                  ? QStringLiteral("no readable structure")
                                                  : warnings.constLast()))
                         .withContext(QJsonObject{{QStringLiteral("path"), absolutePath}});
        return std::nullopt;
    }

    FieldSet sidecar;
    bool usedSidecar = false;
    const QString sidecarPath = sidecarPathFor(absolutePath);
    if (d->exists(sidecarPath)) {
        core::Error sidecarError;
        const QByteArray packet = d->readAll(sidecarPath, &sidecarError);
        if (sidecarError.isError()) {
            warnings.append(QStringLiteral("The sidecar %1 cannot be read: %2")
                                    .arg(QFileInfo(sidecarPath).fileName(),
                                         sidecarError.message()));
        } else {
            QStringList sidecarWarnings;
            if (readXmpPacket(packet, &sidecar, &sidecarWarnings)) {
                usedSidecar = true;
            } else {
                sidecar = FieldSet();
            }
            warnings.append(sidecarWarnings);
        }
    }

    core::MediaMetadata metadata;
    metadata.kind = kindFor(signature);
    metadata.fileName = QFileInfo(absolutePath).fileName();
    metadata.folderPath = QFileInfo(absolutePath).path();

    // Embedded values come from the camera, sidecar values from a person or
    // their tool. The later, deliberate statement wins, and the earlier one is
    // preserved as a conflict rather than discarded.
    std::optional<core::CaptureTime> captureTime;
    core::MetadataOrigin captureTimeOrigin = core::MetadataOrigin::Unknown;
    resolve<core::CaptureTime>(QStringLiteral("captureTime"), embedded.captureTime,
                               core::MetadataOrigin::Embedded, sidecar.captureTime,
                               core::MetadataOrigin::Sidecar, &captureTime, &captureTimeOrigin,
                               &metadata.conflicts,
                               [](const core::CaptureTime &value) { return describe(value); });

    if (!captureTime.has_value()) {
        // Nothing in the file says when it was taken. The filesystem's
        // modification time is a visible, clearly labelled fallback rather
        // than a silent guess.
        const QDateTime modified = d->lastModified(absolutePath);
        if (modified.isValid()) {
            captureTime = core::CaptureTime::fromOffset(modified, modified.offsetFromUtc());
            captureTimeOrigin = core::MetadataOrigin::FileSystem;
        }
    }
    if (captureTime.has_value()) {
        metadata.captureTime = *captureTime;
        metadata.captureTimeOrigin = captureTimeOrigin;
    }

    std::optional<QString> caption;
    core::MetadataOrigin captionOrigin = core::MetadataOrigin::Unknown;
    resolve<QString>(QStringLiteral("caption"), embedded.caption, core::MetadataOrigin::Embedded,
                     sidecar.caption, core::MetadataOrigin::Sidecar, &caption, &captionOrigin,
                     &metadata.conflicts, [](const QString &value) { return value; });
    metadata.caption = caption.value_or(QString());

    std::optional<int> rating;
    core::MetadataOrigin ratingOrigin = core::MetadataOrigin::Unknown;
    resolve<int>(QStringLiteral("rating"), embedded.rating, core::MetadataOrigin::Embedded,
                 sidecar.rating, core::MetadataOrigin::Sidecar, &rating, &ratingOrigin,
                 &metadata.conflicts, [](int value) { return QString::number(value); });
    metadata.rating = rating.value_or(0);

    std::optional<QStringList> tags;
    core::MetadataOrigin tagsOrigin = core::MetadataOrigin::Unknown;
    resolve<QStringList>(QStringLiteral("tags"), embedded.tags, core::MetadataOrigin::Embedded,
                         sidecar.tags, core::MetadataOrigin::Sidecar, &tags, &tagsOrigin,
                         &metadata.conflicts,
                         [](const QStringList &value) { return value.join(QLatin1Char(',')); });
    metadata.tags = tags.value_or(QStringList());

    metadata.cameraMake = embedded.cameraMake.value_or(QString());
    metadata.cameraModel = embedded.cameraModel.value_or(QString());
    metadata.lensModel = embedded.lensModel.value_or(QString());
    metadata.pixelWidth = embedded.pixelWidth.value_or(0);
    metadata.pixelHeight = embedded.pixelHeight.value_or(0);
    metadata.rotationDegrees = embedded.rotationDegrees.value_or(0);
    metadata.durationMs = embedded.durationMs.value_or(0);
    metadata.hasAudio = embedded.hasAudio.value_or(false);
    metadata.location = embedded.location.has_value() ? embedded.location : sidecar.location;
    metadata.normalize();

    core::MetadataReadResult result;
    result.metadata = metadata;
    result.usedSidecar = usedSidecar;
    for (const QString &warning : std::as_const(warnings)) {
        result.warnings.append(
                core::Error(core::ErrorCode::CorruptData, warning)
                        .withContext(QJsonObject{{QStringLiteral("path"), absolutePath}}));
    }
    return result;
}

} // namespace pimio::metadata
