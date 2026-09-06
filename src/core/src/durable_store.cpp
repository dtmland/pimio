#include "pimio/core/durable_store.h"

#include "pimio/core/serialization.h"

namespace pimio::core {
namespace {

constexpr QLatin1StringView kIdKey{"id"};
constexpr QLatin1StringView kFingerprintAlgorithmKey{"fingerprintAlgorithm"};
constexpr QLatin1StringView kFingerprintDigestKey{"fingerprintDigest"};
constexpr QLatin1StringView kIdentityKey{"identity"};
constexpr QLatin1StringView kOriginalStorageKey{"originalStorage"};
constexpr QLatin1StringView kManagedOriginalPathKey{"managedOriginalPath"};
constexpr QLatin1StringView kMetadataKey{"metadata"};
constexpr QLatin1StringView kRecipeKey{"recipe"};
constexpr QLatin1StringView kMessageKey{"message"};
constexpr QLatin1StringView kCreatedAtUtcKey{"createdAtUtc"};
constexpr QLatin1StringView kAuthorIdKey{"authorId"};
constexpr QLatin1StringView kApplicationVersionKey{"applicationVersion"};
constexpr QLatin1StringView kParentIdKey{"parentId"};

} // namespace

QJsonObject Checkpoint::toJson() const
{
    QJsonObject object = unrecognizedFields;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kIdKey, id);
    object.insert(kMessageKey, message);
    object.insert(kCreatedAtUtcKey, createdAtUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(kAuthorIdKey, authorId);
    object.insert(kApplicationVersionKey, applicationVersion);
    object.insert(kParentIdKey, parentId);
    return object;
}

Checkpoint Checkpoint::fromJson(const QJsonObject &object)
{
    Checkpoint checkpoint;
    checkpoint.id = object.value(kIdKey).toString();
    checkpoint.message = object.value(kMessageKey).toString();
    checkpoint.createdAtUtc =
            QDateTime::fromString(object.value(kCreatedAtUtcKey).toString(), Qt::ISODateWithMs)
                    .toUTC();
    checkpoint.authorId = object.value(kAuthorIdKey).toString(QString(kUnknownAuthorId));
    checkpoint.applicationVersion = object.value(kApplicationVersionKey).toString();
    checkpoint.parentId = object.value(kParentIdKey).toString();
    checkpoint.unrecognizedFields =
            unknownFields(object, {kIdKey, kMessageKey, kCreatedAtUtcKey, kAuthorIdKey,
                                   kApplicationVersionKey, kParentIdKey});
    return checkpoint;
}

QJsonObject MediaRecord::toJson() const
{
    QJsonObject object;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kIdKey, id.value());
    object.insert(kFingerprintAlgorithmKey, fingerprint.algorithm());
    object.insert(kFingerprintDigestKey, fingerprint.digest());
    object.insert(kIdentityKey, identity.toJson());
    object.insert(kOriginalStorageKey,
                  originalStorage == OriginalStorage::Managed ? QStringLiteral("managed")
                                                              : QStringLiteral("referenced"));
    object.insert(kManagedOriginalPathKey, managedOriginalPath);
    object.insert(kMetadataKey, metadata.toJson());
    object.insert(kRecipeKey, recipe.toJson());
    return object;
}

MediaRecord MediaRecord::fromJson(const QJsonObject &object)
{
    MediaRecord record;
    record.id = MediaId(object.value(kIdKey).toString());
    record.fingerprint = ContentFingerprint(object.value(kFingerprintAlgorithmKey).toString(),
                                            object.value(kFingerprintDigestKey).toString());
    record.identity = FileIdentity::fromJson(object.value(kIdentityKey).toObject());
    record.originalStorage =
            object.value(kOriginalStorageKey).toString() == QLatin1String("managed")
            ? OriginalStorage::Managed
            : OriginalStorage::Referenced;
    record.managedOriginalPath = object.value(kManagedOriginalPathKey).toString();
    record.metadata = MediaMetadata::fromJson(object.value(kMetadataKey).toObject());
    record.recipe = EditRecipe::fromJson(object.value(kRecipeKey).toObject());
    return record;
}

DurableStore::~DurableStore() = default;

} // namespace pimio::core
