#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace pimio::core {

/// A WGS84 coordinate optionally carrying altitude in metres.
class GeoLocation
{
public:
    GeoLocation() = default;
    GeoLocation(double latitude, double longitude);

    static bool isValidLatitude(double latitude);
    static bool isValidLongitude(double longitude);

    /// Builds a location, returning nothing when the coordinate is outside the
    /// valid WGS84 range or is not a finite number.
    static std::optional<GeoLocation> create(double latitude, double longitude);

    bool isValid() const;
    double latitude() const;
    double longitude() const;

    std::optional<double> altitudeMetres() const;
    void setAltitudeMetres(std::optional<double> altitude);

    bool operator==(const GeoLocation &other) const = default;

    QJsonObject toJson() const;
    static std::optional<GeoLocation> fromJson(const QJsonObject &object);

private:
    bool m_valid = false;
    double m_latitude = 0.0;
    double m_longitude = 0.0;
    std::optional<double> m_altitudeMetres;
};

} // namespace pimio::core
