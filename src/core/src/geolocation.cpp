#include "pimio/core/geolocation.h"

#include <QtNumeric>

#include <cmath>

namespace pimio::core {
namespace {

constexpr QLatin1StringView kLatitudeKey{"latitude"};
constexpr QLatin1StringView kLongitudeKey{"longitude"};
constexpr QLatin1StringView kAltitudeKey{"altitudeMetres"};

} // namespace

GeoLocation::GeoLocation(double latitude, double longitude)
    : m_valid(isValidLatitude(latitude) && isValidLongitude(longitude))
    , m_latitude(latitude)
    , m_longitude(longitude)
{
}

bool GeoLocation::isValidLatitude(double latitude)
{
    return std::isfinite(latitude) && latitude >= -90.0 && latitude <= 90.0;
}

bool GeoLocation::isValidLongitude(double longitude)
{
    return std::isfinite(longitude) && longitude >= -180.0 && longitude <= 180.0;
}

std::optional<GeoLocation> GeoLocation::create(double latitude, double longitude)
{
    if (!isValidLatitude(latitude) || !isValidLongitude(longitude)) {
        return std::nullopt;
    }
    return GeoLocation(latitude, longitude);
}

bool GeoLocation::isValid() const
{
    return m_valid;
}

double GeoLocation::latitude() const
{
    return m_latitude;
}

double GeoLocation::longitude() const
{
    return m_longitude;
}

std::optional<double> GeoLocation::altitudeMetres() const
{
    return m_altitudeMetres;
}

void GeoLocation::setAltitudeMetres(std::optional<double> altitude)
{
    if (altitude && !std::isfinite(*altitude)) {
        m_altitudeMetres.reset();
        return;
    }
    m_altitudeMetres = altitude;
}

QJsonObject GeoLocation::toJson() const
{
    QJsonObject object;
    object.insert(kLatitudeKey, m_latitude);
    object.insert(kLongitudeKey, m_longitude);
    if (m_altitudeMetres) {
        object.insert(kAltitudeKey, *m_altitudeMetres);
    }
    return object;
}

std::optional<GeoLocation> GeoLocation::fromJson(const QJsonObject &object)
{
    if (!object.contains(kLatitudeKey) || !object.contains(kLongitudeKey)) {
        return std::nullopt;
    }
    auto location = create(object.value(kLatitudeKey).toDouble(qQNaN()),
                           object.value(kLongitudeKey).toDouble(qQNaN()));
    if (!location) {
        return std::nullopt;
    }
    if (object.contains(kAltitudeKey)) {
        location->setAltitudeMetres(object.value(kAltitudeKey).toDouble(qQNaN()));
    }
    return location;
}

} // namespace pimio::core
