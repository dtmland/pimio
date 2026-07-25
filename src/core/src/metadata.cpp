#include "pimio/core/metadata.h"

#include "pimio/core/serialization.h"

#include <QJsonArray>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace pimio::core {
namespace {

constexpr QLatin1StringView kWallClockKey{"wallClock"};
constexpr QLatin1StringView kUtcOffsetKey{"utcOffsetSeconds"};

constexpr QLatin1StringView kFieldKey{"field"};
constexpr QLatin1StringView kPreferredOriginKey{"preferredOrigin"};
constexpr QLatin1StringView kConflictingOriginKey{"conflictingOrigin"};
constexpr QLatin1StringView kPreferredValueKey{"preferredValue"};
constexpr QLatin1StringView kConflictingValueKey{"conflictingValue"};

constexpr QLatin1StringView kKindKey{"kind"};
constexpr QLatin1StringView kFileNameKey{"fileName"};
constexpr QLatin1StringView kFolderPathKey{"folderPath"};
constexpr QLatin1StringView kCaptureTimeKey{"captureTime"};
constexpr QLatin1StringView kCaptureTimeOriginKey{"captureTimeOrigin"};
constexpr QLatin1StringView kCameraMakeKey{"cameraMake"};
constexpr QLatin1StringView kCameraModelKey{"cameraModel"};
constexpr QLatin1StringView kLensModelKey{"lensModel"};
constexpr QLatin1StringView kPixelWidthKey{"pixelWidth"};
constexpr QLatin1StringView kPixelHeightKey{"pixelHeight"};
constexpr QLatin1StringView kRotationKey{"rotationDegrees"};
constexpr QLatin1StringView kDurationKey{"durationMs"};
constexpr QLatin1StringView kHasAudioKey{"hasAudio"};
constexpr QLatin1StringView kLocationKey{"location"};
constexpr QLatin1StringView kRatingKey{"rating"};
constexpr QLatin1StringView kCaptionKey{"caption"};
constexpr QLatin1StringView kTagsKey{"tags"};
constexpr QLatin1StringView kConflictsKey{"conflicts"};

} // namespace

QString toString(MetadataOrigin origin)
{
    switch (origin) {
    case MetadataOrigin::FileSystem:
        return QStringLiteral("fileSystem");
    case MetadataOrigin::Embedded:
        return QStringLiteral("embedded");
    case MetadataOrigin::Sidecar:
        return QStringLiteral("sidecar");
    case MetadataOrigin::UserEdit:
        return QStringLiteral("userEdit");
    case MetadataOrigin::Inferred:
        return QStringLiteral("inferred");
    case MetadataOrigin::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

MetadataOrigin metadataOriginFromString(const QString &value)
{
    if (value == QLatin1StringView("fileSystem")) {
        return MetadataOrigin::FileSystem;
    }
    if (value == QLatin1StringView("embedded")) {
        return MetadataOrigin::Embedded;
    }
    if (value == QLatin1StringView("sidecar")) {
        return MetadataOrigin::Sidecar;
    }
    if (value == QLatin1StringView("userEdit")) {
        return MetadataOrigin::UserEdit;
    }
    if (value == QLatin1StringView("inferred")) {
        return MetadataOrigin::Inferred;
    }
    return MetadataOrigin::Unknown;
}

CaptureTime CaptureTime::fromLocalWallClock(const QDateTime &wallClock)
{
    CaptureTime time;
    time.m_wallClock = wallClock;
    time.m_wallClock.setTimeSpec(Qt::LocalTime);
    return time;
}

CaptureTime CaptureTime::fromOffset(const QDateTime &wallClock, int utcOffsetSeconds)
{
    CaptureTime time;
    time.m_wallClock = wallClock;
    time.m_wallClock.setTimeSpec(Qt::LocalTime);
    time.m_utcOffsetSeconds = utcOffsetSeconds;
    return time;
}

bool CaptureTime::isValid() const
{
    return m_wallClock.isValid();
}

const QDateTime &CaptureTime::wallClock() const
{
    return m_wallClock;
}

std::optional<int> CaptureTime::utcOffsetSeconds() const
{
    return m_utcOffsetSeconds;
}

bool CaptureTime::hasKnownOffset() const
{
    return m_utcOffsetSeconds.has_value();
}

std::optional<QDateTime> CaptureTime::toUtc() const
{
    if (!isValid() || !m_utcOffsetSeconds) {
        return std::nullopt;
    }
    QDateTime resolved = m_wallClock;
    resolved.setTimeZone(QTimeZone(*m_utcOffsetSeconds));
    return resolved.toUTC();
}

qint64 CaptureTime::sortKeyMSecs() const
{
    if (!isValid()) {
        return std::numeric_limits<qint64>::min();
    }
    QDateTime asUtc = m_wallClock;
    asUtc.setTimeSpec(Qt::UTC);
    return asUtc.toMSecsSinceEpoch();
}

QJsonObject CaptureTime::toJson() const
{
    QJsonObject object;
    object.insert(kWallClockKey,
                  m_wallClock.isValid() ? m_wallClock.toString(Qt::ISODateWithMs) : QString());
    if (m_utcOffsetSeconds) {
        object.insert(kUtcOffsetKey, *m_utcOffsetSeconds);
    }
    return object;
}

CaptureTime CaptureTime::fromJson(const QJsonObject &object)
{
    CaptureTime time;
    const QString wallClock = object.value(kWallClockKey).toString();
    if (!wallClock.isEmpty()) {
        time.m_wallClock = QDateTime::fromString(wallClock, Qt::ISODateWithMs);
        time.m_wallClock.setTimeSpec(Qt::LocalTime);
    }
    if (object.contains(kUtcOffsetKey)) {
        time.m_utcOffsetSeconds = object.value(kUtcOffsetKey).toInt();
    }
    return time;
}

QJsonObject MetadataConflict::toJson() const
{
    QJsonObject object;
    object.insert(kFieldKey, field);
    object.insert(kPreferredOriginKey, toString(preferredOrigin));
    object.insert(kConflictingOriginKey, toString(conflictingOrigin));
    object.insert(kPreferredValueKey, preferredValue);
    object.insert(kConflictingValueKey, conflictingValue);
    return object;
}

MetadataConflict MetadataConflict::fromJson(const QJsonObject &object)
{
    MetadataConflict conflict;
    conflict.field = object.value(kFieldKey).toString();
    conflict.preferredOrigin =
            metadataOriginFromString(object.value(kPreferredOriginKey).toString());
    conflict.conflictingOrigin =
            metadataOriginFromString(object.value(kConflictingOriginKey).toString());
    conflict.preferredValue = object.value(kPreferredValueKey).toString();
    conflict.conflictingValue = object.value(kConflictingValueKey).toString();
    return conflict;
}

void MediaMetadata::normalize()
{
    int rotation = rotationDegrees % 360;
    if (rotation < 0) {
        rotation += 360;
    }
    rotation = ((rotation + 45) / 90 % 4) * 90;
    rotationDegrees = rotation;

    rating = std::clamp(rating, 0, 5);

    pixelWidth = std::max(pixelWidth, 0);
    pixelHeight = std::max(pixelHeight, 0);
    durationMs = std::max<qint64>(durationMs, 0);

    if (location && !location->isValid()) {
        location.reset();
    }
}

QStringList MediaMetadata::knownKeys()
{
    return {
        kKindKey,      kFileNameKey,    kFolderPathKey, kCaptureTimeKey, kCaptureTimeOriginKey,
        kCameraMakeKey, kCameraModelKey, kLensModelKey,  kPixelWidthKey,  kPixelHeightKey,
        kRotationKey,  kDurationKey,    kHasAudioKey,   kLocationKey,    kRatingKey,
        kCaptionKey,   kTagsKey,        kConflictsKey,
    };
}

const QJsonObject &MediaMetadata::unrecognizedFields() const
{
    return m_unrecognizedFields;
}

void MediaMetadata::setUnrecognizedFields(QJsonObject fields)
{
    m_unrecognizedFields = std::move(fields);
}

QJsonObject MediaMetadata::toJson() const
{
    QJsonObject object;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kKindKey, toString(kind));
    object.insert(kFileNameKey, fileName);
    object.insert(kFolderPathKey, folderPath);
    object.insert(kCaptureTimeKey, captureTime.toJson());
    object.insert(kCaptureTimeOriginKey, toString(captureTimeOrigin));
    object.insert(kCameraMakeKey, cameraMake);
    object.insert(kCameraModelKey, cameraModel);
    object.insert(kLensModelKey, lensModel);
    object.insert(kPixelWidthKey, pixelWidth);
    object.insert(kPixelHeightKey, pixelHeight);
    object.insert(kRotationKey, rotationDegrees);
    object.insert(kDurationKey, durationMs);
    object.insert(kHasAudioKey, hasAudio);
    if (location) {
        object.insert(kLocationKey, location->toJson());
    }
    object.insert(kRatingKey, rating);
    object.insert(kCaptionKey, caption);
    object.insert(kTagsKey, QJsonArray::fromStringList(tags));

    QJsonArray conflictArray;
    for (const MetadataConflict &conflict : conflicts) {
        conflictArray.append(conflict.toJson());
    }
    object.insert(kConflictsKey, conflictArray);

    mergeUnknownFields(object, m_unrecognizedFields);
    return object;
}

MediaMetadata MediaMetadata::fromJson(const QJsonObject &object)
{
    MediaMetadata metadata;
    metadata.kind = mediaKindFromString(object.value(kKindKey).toString());
    metadata.fileName = object.value(kFileNameKey).toString();
    metadata.folderPath = object.value(kFolderPathKey).toString();
    metadata.captureTime = CaptureTime::fromJson(object.value(kCaptureTimeKey).toObject());
    metadata.captureTimeOrigin =
            metadataOriginFromString(object.value(kCaptureTimeOriginKey).toString());
    metadata.cameraMake = object.value(kCameraMakeKey).toString();
    metadata.cameraModel = object.value(kCameraModelKey).toString();
    metadata.lensModel = object.value(kLensModelKey).toString();
    metadata.pixelWidth = object.value(kPixelWidthKey).toInt();
    metadata.pixelHeight = object.value(kPixelHeightKey).toInt();
    metadata.rotationDegrees = object.value(kRotationKey).toInt();
    metadata.durationMs = static_cast<qint64>(object.value(kDurationKey).toDouble());
    metadata.hasAudio = object.value(kHasAudioKey).toBool();
    if (object.contains(kLocationKey)) {
        metadata.location = GeoLocation::fromJson(object.value(kLocationKey).toObject());
    }
    metadata.rating = object.value(kRatingKey).toInt();
    metadata.caption = object.value(kCaptionKey).toString();

    const QJsonArray tagArray = object.value(kTagsKey).toArray();
    for (const QJsonValue &tag : tagArray) {
        metadata.tags.append(tag.toString());
    }

    const QJsonArray conflictArray = object.value(kConflictsKey).toArray();
    for (const QJsonValue &conflict : conflictArray) {
        metadata.conflicts.append(MetadataConflict::fromJson(conflict.toObject()));
    }

    metadata.setUnrecognizedFields(unknownFields(object, knownKeys()));
    return metadata;
}

} // namespace pimio::core
