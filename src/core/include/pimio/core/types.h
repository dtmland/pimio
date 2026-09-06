#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace pimio::core {

/// Broad classification of a library item.
enum class MediaKind {
    Unknown,
    Image,
    Video,
};

QString toString(MediaKind kind);
MediaKind mediaKindFromString(const QString &value);

/// Stable, opaque identifier for a library item.
///
/// The identifier stays the same when a file is renamed or moved. It is not
/// derived from the path so that reorganizing a library does not create new
/// items.
class MediaId
{
public:
    MediaId() = default;
    explicit MediaId(QString value);

    /// Generates a new random identifier.
    static MediaId generate();

    bool isValid() const;
    const QString &value() const;

    bool operator==(const MediaId &other) const = default;

private:
    QString m_value;
};

size_t qHash(const MediaId &id, size_t seed = 0);

/// Content-derived fingerprint used to recognize moves, renames, and
/// duplicates, and to key derived caches such as thumbnails.
class ContentFingerprint
{
public:
    ContentFingerprint() = default;
    ContentFingerprint(QString algorithm, QString digest);

    bool isValid() const;
    const QString &algorithm() const;
    const QString &digest() const;

    /// Cache key combining algorithm and digest. Stable across runs.
    QString cacheKey() const;

    bool operator==(const ContentFingerprint &other) const = default;

private:
    QString m_algorithm;
    QString m_digest;
};

/// Filesystem-level facts about a file, used to detect cheap "unchanged" cases
/// before a fingerprint is recomputed.
///
/// \c volumeId and \c fileId are the platform inode/file-index pair when the
/// filesystem provides them. They are optional because network and FAT-family
/// volumes may not.
struct FileIdentity
{
    QString absolutePath;
    QString volumeId;
    QString fileId;
    qint64 sizeBytes = -1;
    QDateTime lastModified;

    bool isValid() const;

    /// True when both identities carry a volume/file pair and the pairs match.
    /// Path is deliberately ignored so a rename is still the same file.
    bool sameFileAs(const FileIdentity &other) const;

    /// True when size and modification time are unchanged. This is a cheap
    /// pre-check only; it is never sufficient evidence on its own.
    bool looksUnchangedFrom(const FileIdentity &other) const;

    QJsonObject toJson() const;
    static FileIdentity fromJson(const QJsonObject &object);

    bool operator==(const FileIdentity &other) const = default;
};

} // namespace pimio::core
