#pragma once

#include "pimio/core/geolocation.h"
#include "pimio/core/metadata.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace pimio::metadata {

/// Values read from one source, before precedence is applied.
///
/// Every field a source may legitimately omit is optional so that "absent" and
/// "present but empty" stay distinguishable. Precedence needs that distinction:
/// an empty caption in a sidecar is a deliberate erasure, a missing one is not
/// an opinion at all.
struct FieldSet
{
    std::optional<core::CaptureTime> captureTime;

    std::optional<QString> cameraMake;
    std::optional<QString> cameraModel;
    std::optional<QString> lensModel;

    std::optional<int> pixelWidth;
    std::optional<int> pixelHeight;
    std::optional<int> rotationDegrees;

    std::optional<qint64> durationMs;
    std::optional<bool> hasAudio;

    std::optional<core::GeoLocation> location;

    std::optional<int> rating;
    std::optional<QString> caption;
    std::optional<QStringList> tags;
};

} // namespace pimio::metadata
