#pragma once

#include "pimio/core/geolocation.h"
#include "pimio/core/types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace pimio::core {

/// Where a metadata value came from. Precedence is resolved by callers, not by
/// the value itself, so a conflict can always be shown to the user.
enum class MetadataOrigin {
    Unknown,
    FileSystem,   ///< Derived from the file itself, e.g. modification time.
    Embedded,     ///< EXIF/IPTC/XMP stored inside the media file.
    Sidecar,      ///< Adjacent XMP sidecar.
    UserEdit,     ///< Explicitly set by the user.
    Inferred,     ///< Suggested by pimio and not yet confirmed.
};

QString toString(MetadataOrigin origin);
MetadataOrigin metadataOriginFromString(const QString &value);

/// A capture time together with what is actually known about its time zone.
///
/// Many libraries contain timestamps with no zone information at all. Modelling
/// that explicitly prevents pimio from inventing an offset and then presenting
/// it as fact.
class CaptureTime
{
public:
    CaptureTime() = default;

    /// A local wall-clock time with no known UTC offset.
    static CaptureTime fromLocalWallClock(const QDateTime &wallClock);

    /// A time with a known UTC offset in seconds.
    static CaptureTime fromOffset(const QDateTime &wallClock, int utcOffsetSeconds);

    bool isValid() const;

    /// The wall-clock time as recorded, without any zone conversion applied.
    const QDateTime &wallClock() const;

    /// The known UTC offset in seconds, if any.
    std::optional<int> utcOffsetSeconds() const;

    bool hasKnownOffset() const;

    /// The instant in UTC. Only available when the offset is known; otherwise a
    /// caller must decide how to interpret the wall clock.
    std::optional<QDateTime> toUtc() const;

    /// Deterministic sort key. Items without a known offset sort by wall clock,
    /// which keeps ordering stable even in mixed libraries.
    qint64 sortKeyMSecs() const;

    bool operator==(const CaptureTime &other) const = default;

    QJsonObject toJson() const;
    static CaptureTime fromJson(const QJsonObject &object);

private:
    QDateTime m_wallClock;
    std::optional<int> m_utcOffsetSeconds;
};

/// A metadata value that two sources disagree about.
struct MetadataConflict
{
    QString field;
    MetadataOrigin preferredOrigin = MetadataOrigin::Unknown;
    MetadataOrigin conflictingOrigin = MetadataOrigin::Unknown;
    QString preferredValue;
    QString conflictingValue;

    bool operator==(const MetadataConflict &other) const = default;

    QJsonObject toJson() const;
    static MetadataConflict fromJson(const QJsonObject &object);
};

/// UI-independent metadata for a single image or video.
///
/// Unrecognized fields read from storage are preserved so that a record written
/// by a newer pimio survives a read/modify/write cycle by an older one.
class MediaMetadata
{
public:
    MediaKind kind = MediaKind::Unknown;
    QString fileName;
    QString folderPath;

    CaptureTime captureTime;
    MetadataOrigin captureTimeOrigin = MetadataOrigin::Unknown;

    QString cameraMake;
    QString cameraModel;
    QString lensModel;

    int pixelWidth = 0;
    int pixelHeight = 0;

    /// Clockwise display rotation in degrees: 0, 90, 180, or 270.
    int rotationDegrees = 0;

    /// Video duration in milliseconds. Zero for still images.
    qint64 durationMs = 0;
    bool hasAudio = false;

    std::optional<GeoLocation> location;

    /// 0 to 5, where 0 means unrated.
    int rating = 0;
    QString caption;
    QStringList tags;

    QList<MetadataConflict> conflicts;

    /// Normalizes rotation to one of 0, 90, 180, 270 and clamps the rating.
    void normalize();

    bool operator==(const MediaMetadata &other) const = default;

    QJsonObject toJson() const;
    static MediaMetadata fromJson(const QJsonObject &object);

    /// Fields written by a newer schema version that this build did not
    /// recognize. They are re-emitted verbatim by toJson().
    const QJsonObject &unrecognizedFields() const;
    void setUnrecognizedFields(QJsonObject fields);

    static QStringList knownKeys();

private:
    QJsonObject m_unrecognizedFields;
};

} // namespace pimio::core
