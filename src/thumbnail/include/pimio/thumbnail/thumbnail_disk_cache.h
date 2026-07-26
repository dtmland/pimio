#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace pimio::core {
class ContentFingerprint;
}

namespace pimio::thumbnail {

/// Persistent, fingerprint-keyed cache for rendered thumbnail and preview bytes.
///
/// Entries are stored as files under a single root directory, organised by
/// the cache key of the originating request. The path hierarchy mirrors the
/// key structure so all entries for a fingerprint can be removed with one
/// directory deletion.
///
/// Corrupt entries (zero-byte or unreadable files) are treated as misses and
/// are silently replaced the next time store() is called. The cache is always
/// regenerable so a failed write or read is never a hard error.
///
/// Thread safety: contains(), load(), and store() are each individually safe
/// to call from any thread simultaneously because each is a single atomic
/// filesystem operation. trim() is not thread-safe with store(); call it from
/// a background maintenance pass rather than from a hot render path.
class ThumbnailDiskCache
{
public:
    /// Creates a cache rooted at \a cacheDir with an optional size limit.
    ///
    /// \a maxSizeBytes is advisory: the cache does not proactively trim, but
    /// trim() enforces it. Pass a negative value to disable the size limit.
    explicit ThumbnailDiskCache(const QString &cacheDir,
                                qint64 maxSizeBytes = 512LL * 1024 * 1024);

    const QString &cacheDir() const;

    /// True when a non-empty, readable entry for \a cacheKey exists.
    ///
    /// A zero-byte or unreadable file is treated as a corrupt entry and
    /// returns false. The next store() for the same key replaces it.
    bool contains(const QString &cacheKey) const;

    /// Returns the cached bytes for \a cacheKey.
    ///
    /// Returns \c std::nullopt when the entry is absent or corrupt.
    std::optional<QByteArray> load(const QString &cacheKey) const;

    /// Writes \a bytes under \a cacheKey atomically.
    ///
    /// Writes to a sibling temporary file and renames it over the target so
    /// a concurrent reader never observes a partially written entry. Returns
    /// false when the write fails; the cache state remains consistent.
    bool store(const QString &cacheKey, const QByteArray &bytes);

    /// Removes all entries whose cache key starts with \a fingerprint.cacheKey().
    ///
    /// Removes the fingerprint's subdirectory, which contains every size,
    /// kind, and position variant for that content. Call this after an edit
    /// recipe changes so stale rendered results are evicted.
    void invalidate(const core::ContentFingerprint &fingerprint);

    /// Evicts least-recently-written entries until the total cache size is
    /// within the configured limit.
    ///
    /// Size is measured per file; filesystem metadata overhead is not counted.
    /// When no size limit is configured the call is a no-op.
    void trim();

    /// Approximate total size of all cached files in bytes.
    qint64 totalSizeBytes() const;

private:
    QString pathFor(const QString &cacheKey) const;

    QString m_cacheDir;
    qint64 m_maxSizeBytes;
};

} // namespace pimio::thumbnail
