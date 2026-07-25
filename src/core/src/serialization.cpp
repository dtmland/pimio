#include "pimio/core/serialization.h"

namespace pimio::core {

QJsonObject unknownFields(const QJsonObject &object, const QStringList &knownKeys)
{
    QJsonObject unknown;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.key() == kSchemaVersionKey) {
            continue;
        }
        if (knownKeys.contains(it.key())) {
            continue;
        }
        unknown.insert(it.key(), it.value());
    }
    return unknown;
}

void mergeUnknownFields(QJsonObject &target, const QJsonObject &unknown)
{
    for (auto it = unknown.constBegin(); it != unknown.constEnd(); ++it) {
        if (target.contains(it.key())) {
            continue;
        }
        target.insert(it.key(), it.value());
    }
}

} // namespace pimio::core
