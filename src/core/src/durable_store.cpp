#include "pimio/core/durable_store.h"

#include "pimio/core/serialization.h"

namespace pimio::core {
namespace {

constexpr QLatin1StringView kIdKey{"id"};
constexpr QLatin1StringView kFingerprintAlgorithmKey{"fingerprintAlgorithm"};
constexpr QLatin1StringView kFingerprintDigestKey{"fingerprintDigest"};
constexpr QLatin1StringView kIdentityKey{"identity"};
constexpr QLatin1StringView kMetadataKey{"metadata"};
constexpr QLatin1StringView kRecipeKey{"recipe"};

} // namespace

QJsonObject MediaRecord::toJson() const
{
    QJsonObject object;
    object.insert(kSchemaVersionKey, kRecordSchemaVersion);
    object.insert(kIdKey, id.value());
    object.insert(kFingerprintAlgorithmKey, fingerprint.algorithm());
    object.insert(kFingerprintDigestKey, fingerprint.digest());
    object.insert(kIdentityKey, identity.toJson());
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
    record.metadata = MediaMetadata::fromJson(object.value(kMetadataKey).toObject());
    record.recipe = EditRecipe::fromJson(object.value(kRecipeKey).toObject());
    return record;
}

DurableStore::~DurableStore() = default;

} // namespace pimio::core
