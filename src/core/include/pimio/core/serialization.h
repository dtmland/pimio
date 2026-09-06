#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace pimio::core {

/// Schema version written into every serialized core record.
///
/// Increment this only together with a documented migration. Readers must
/// accept older versions and must preserve unknown fields from newer versions
/// so that a downgrade does not silently discard data.
inline constexpr int kRecordSchemaVersion = 2;

/// Key used for the schema version in every serialized record.
inline constexpr QLatin1StringView kSchemaVersionKey{"schemaVersion"};

/// Collects the members of \a object whose keys are not in \a knownKeys.
///
/// Types call this while parsing so that fields written by a newer version of
/// pimio survive a read/modify/write cycle instead of being dropped.
QJsonObject unknownFields(const QJsonObject &object, const QStringList &knownKeys);

/// Copies every member of \a unknown into \a target without overwriting keys
/// that \a target already defines.
void mergeUnknownFields(QJsonObject &target, const QJsonObject &unknown);

} // namespace pimio::core
