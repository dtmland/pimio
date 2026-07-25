#pragma once

#include <QByteArray>
#include <QString>

namespace pimio::fixtures {

/// Description of the EXIF block written into a generated JPEG fixture.
struct ExifSpec
{
    QString make;
    QString model;
    /// EXIF orientation value, 1 to 8.
    quint16 orientation = 1;
    /// "YYYY:MM:DD HH:MM:SS" as EXIF requires, or empty to omit the tag.
    QString dateTimeOriginal;
    /// "+HH:MM" offset, or empty to omit the tag. Omitting it is the common
    /// real-world case that pimio must not silently treat as UTC.
    QString offsetTimeOriginal;

    bool hasGps = false;
    double latitude = 0.0;
    double longitude = 0.0;
};

/// Builds a complete APP1 EXIF segment, including marker and length.
QByteArray buildExifApp1(const ExifSpec &spec);

/// Returns \a jpeg with \a app1Segment as its first application segment.
/// Returns an empty array when \a jpeg is not a JPEG.
QByteArray injectExif(const QByteArray &jpeg, const QByteArray &app1Segment);

} // namespace pimio::fixtures
