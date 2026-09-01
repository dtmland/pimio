#include "pimio/core/library.h"

#include "pimio/core/serialization.h"

#include <QUuid>

namespace pimio::core {
namespace {

constexpr QLatin1StringView kIdKey{"id"};
constexpr QLatin1StringView kNameKey{"name"};
constexpr QLatin1StringView kFormatVersionKey{"formatVersion"};
constexpr QLatin1StringView kCreatedAtUtcKey{"createdAtUtc"};
constexpr QLatin1StringView kLocalUserKey{"localUser"};
constexpr QLatin1StringView kDisplayNameKey{"displayName"};

} // namespace

QJsonObject LibraryUser::toJson() const
{
    return {{kIdKey, id}, {kDisplayNameKey, displayName}};
}

LibraryUser LibraryUser::fromJson(const QJsonObject &object)
{
    return {object.value(kIdKey).toString(), object.value(kDisplayNameKey).toString()};
}

bool LibraryDescriptor::isValid() const
{
    return !id.isEmpty() && !name.isEmpty() && formatVersion > 0 && createdAtUtc.isValid()
           && !localUser.id.isEmpty();
}

QJsonObject LibraryDescriptor::toJson() const
{
    QJsonObject object = unrecognizedFields;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kIdKey, id);
    object.insert(kNameKey, name);
    object.insert(kFormatVersionKey, formatVersion);
    object.insert(kCreatedAtUtcKey, createdAtUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(kLocalUserKey, localUser.toJson());
    return object;
}

LibraryDescriptor LibraryDescriptor::fromJson(const QJsonObject &object)
{
    LibraryDescriptor descriptor;
    descriptor.id = object.value(kIdKey).toString();
    descriptor.name = object.value(kNameKey).toString();
    descriptor.formatVersion = object.value(kFormatVersionKey).toInt(kLibraryFormatVersion);
    descriptor.createdAtUtc =
            QDateTime::fromString(object.value(kCreatedAtUtcKey).toString(), Qt::ISODateWithMs)
                    .toUTC();
    descriptor.localUser = LibraryUser::fromJson(object.value(kLocalUserKey).toObject());
    descriptor.unrecognizedFields =
            unknownFields(object, {kIdKey, kNameKey, kFormatVersionKey, kCreatedAtUtcKey,
                                   kLocalUserKey});
    return descriptor;
}

bool hasLibraryPermission(const LibraryDescriptor &library, const QString &userId,
                          LibraryPermission)
{
    return library.isValid() && !userId.isEmpty() && userId == library.localUser.id;
}

LibraryDescriptor createLibraryDescriptor(const QString &name, const QDateTime &createdAtUtc)
{
    LibraryDescriptor descriptor;
    descriptor.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    descriptor.name = name.trimmed().isEmpty() ? QStringLiteral("Library") : name.trimmed();
    descriptor.createdAtUtc = createdAtUtc.toUTC();
    descriptor.localUser.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    descriptor.localUser.displayName = QStringLiteral("Local user");
    return descriptor;
}

} // namespace pimio::core
