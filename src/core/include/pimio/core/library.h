#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace pimio::core {

inline constexpr QLatin1StringView kUnknownAuthorId{"unknown"};
inline constexpr int kLibraryFormatVersion = 1;

struct LibraryUser
{
    QString id;
    QString displayName;

    QJsonObject toJson() const;
    static LibraryUser fromJson(const QJsonObject &object);

    bool operator==(const LibraryUser &other) const = default;
};

/// Stable identity and format information stored inside a library repository.
struct LibraryDescriptor
{
    QString id;
    QString name;
    int formatVersion = kLibraryFormatVersion;
    QDateTime createdAtUtc;
    LibraryUser localUser;
    QJsonObject unrecognizedFields;

    bool isValid() const;
    QJsonObject toJson() const;
    static LibraryDescriptor fromJson(const QJsonObject &object);

    bool operator==(const LibraryDescriptor &other) const = default;
};

enum class LibraryPermission {
    Read,
    Write,
    Administer,
    Share,
};

/// v1's authorization boundary. The implicit local user has every permission;
/// unknown users have none. Later service implementations can replace this
/// policy without changing callers or stored author identities.
bool hasLibraryPermission(const LibraryDescriptor &library, const QString &userId,
                          LibraryPermission permission);

LibraryDescriptor createLibraryDescriptor(const QString &name, const QDateTime &createdAtUtc);

} // namespace pimio::core
