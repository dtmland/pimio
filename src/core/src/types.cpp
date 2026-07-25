#include "pimio/core/types.h"

#include <QHash>
#include <QUuid>

namespace pimio::core {
namespace {

constexpr QLatin1StringView kAbsolutePathKey{"absolutePath"};
constexpr QLatin1StringView kVolumeIdKey{"volumeId"};
constexpr QLatin1StringView kFileIdKey{"fileId"};
constexpr QLatin1StringView kSizeBytesKey{"sizeBytes"};
constexpr QLatin1StringView kLastModifiedKey{"lastModified"};

} // namespace

QString toString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

MediaKind mediaKindFromString(const QString &value)
{
    if (value == QLatin1StringView("image")) {
        return MediaKind::Image;
    }
    if (value == QLatin1StringView("video")) {
        return MediaKind::Video;
    }
    return MediaKind::Unknown;
}

MediaId::MediaId(QString value)
    : m_value(std::move(value))
{
}

MediaId MediaId::generate()
{
    return MediaId(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool MediaId::isValid() const
{
    return !m_value.isEmpty();
}

const QString &MediaId::value() const
{
    return m_value;
}

size_t qHash(const MediaId &id, size_t seed)
{
    return qHash(id.value(), seed);
}

ContentFingerprint::ContentFingerprint(QString algorithm, QString digest)
    : m_algorithm(std::move(algorithm))
    , m_digest(std::move(digest))
{
}

bool ContentFingerprint::isValid() const
{
    return !m_algorithm.isEmpty() && !m_digest.isEmpty();
}

const QString &ContentFingerprint::algorithm() const
{
    return m_algorithm;
}

const QString &ContentFingerprint::digest() const
{
    return m_digest;
}

QString ContentFingerprint::cacheKey() const
{
    if (!isValid()) {
        return QString();
    }
    return m_algorithm + QLatin1Char('-') + m_digest;
}

bool FileIdentity::isValid() const
{
    return !absolutePath.isEmpty() && sizeBytes >= 0;
}

bool FileIdentity::sameFileAs(const FileIdentity &other) const
{
    if (volumeId.isEmpty() || fileId.isEmpty()) {
        return false;
    }
    return volumeId == other.volumeId && fileId == other.fileId;
}

bool FileIdentity::looksUnchangedFrom(const FileIdentity &other) const
{
    if (sizeBytes < 0 || other.sizeBytes < 0) {
        return false;
    }
    if (!lastModified.isValid() || !other.lastModified.isValid()) {
        return false;
    }
    return sizeBytes == other.sizeBytes && lastModified == other.lastModified;
}

QJsonObject FileIdentity::toJson() const
{
    QJsonObject object;
    object.insert(kAbsolutePathKey, absolutePath);
    object.insert(kVolumeIdKey, volumeId);
    object.insert(kFileIdKey, fileId);
    object.insert(kSizeBytesKey, sizeBytes);
    object.insert(kLastModifiedKey,
                  lastModified.isValid() ? lastModified.toUTC().toString(Qt::ISODateWithMs)
                                         : QString());
    return object;
}

FileIdentity FileIdentity::fromJson(const QJsonObject &object)
{
    FileIdentity identity;
    identity.absolutePath = object.value(kAbsolutePathKey).toString();
    identity.volumeId = object.value(kVolumeIdKey).toString();
    identity.fileId = object.value(kFileIdKey).toString();
    identity.sizeBytes = static_cast<qint64>(object.value(kSizeBytesKey).toDouble(-1));
    const QString lastModified = object.value(kLastModifiedKey).toString();
    if (!lastModified.isEmpty()) {
        identity.lastModified = QDateTime::fromString(lastModified, Qt::ISODateWithMs).toUTC();
    }
    return identity;
}

} // namespace pimio::core
