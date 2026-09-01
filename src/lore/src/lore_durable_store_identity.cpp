#include "lore_durable_store_private.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace pimio::lore {

using core::Error;
using core::ErrorCode;

bool LoreDurableStore::createLibrary(const QString &name, Error *error)
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                         QStringLiteral("The durable store is unavailable."));
        return false;
    }
    Error descriptorError;
    if (libraryDescriptor(&descriptorError)) {
        detail::setError(error, ErrorCode::Conflict,
                         QStringLiteral("The repository already has a library descriptor."));
        return false;
    }
    if (descriptorError.code() != ErrorCode::NotFound) {
        if (error) {
            *error = descriptorError;
        }
        return false;
    }

    const core::LibraryDescriptor descriptor =
            core::createLibraryDescriptor(name, QDateTime::currentDateTimeUtc());
    QSaveFile file(d->stagedLibraryDescriptorPath());
    const QByteArray bytes = QJsonDocument(descriptor.toJson()).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        detail::setError(error, ErrorCode::PermissionDenied,
                         QStringLiteral("Could not stage the library descriptor."));
        return false;
    }
    return commit(QStringLiteral("Create library"), error).has_value();
}

std::optional<core::LibraryDescriptor>
LoreDurableStore::libraryDescriptor(Error *error) const
{
    if (!d->available()) {
        detail::setError(error, ErrorCode::StorageUnavailable,
                         QStringLiteral("The durable store is unavailable."));
        return std::nullopt;
    }
    QFile file(d->libraryDescriptorPath());
    if (!file.exists()) {
        detail::setError(error, ErrorCode::NotFound,
                         QStringLiteral("The repository has no library descriptor."));
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        detail::setError(error, ErrorCode::PermissionDenied,
                         QStringLiteral("Could not read the library descriptor."));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const core::LibraryDescriptor descriptor =
            core::LibraryDescriptor::fromJson(document.object());
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !descriptor.isValid()) {
        detail::setError(error, ErrorCode::CorruptData,
                         QStringLiteral("The library descriptor is invalid."));
        return std::nullopt;
    }
    return descriptor;
}

} // namespace pimio::lore
