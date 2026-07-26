#include "pimio/thumbnail/thumbnail_disk_cache.h"

#include "pimio/core/types.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace pimio::thumbnail {

ThumbnailDiskCache::ThumbnailDiskCache(const QString &cacheDir, qint64 maxSizeBytes)
    : m_cacheDir(cacheDir)
    , m_maxSizeBytes(maxSizeBytes)
{
}

const QString &ThumbnailDiskCache::cacheDir() const
{
    return m_cacheDir;
}

// Turns a cache key such as "sha256-abc.../thumbnail/160x160/0/r0" into a
// filesystem path under the cache root. Forward slashes in the key become
// platform directory separators so the hierarchy is preserved on all platforms.
QString ThumbnailDiskCache::pathFor(const QString &cacheKey) const
{
    QString relative = cacheKey;
    relative.replace(QLatin1Char('/'), QDir::separator());
    return m_cacheDir + QDir::separator() + relative;
}

bool ThumbnailDiskCache::contains(const QString &cacheKey) const
{
    const QFileInfo info(pathFor(cacheKey));
    return info.exists() && info.isFile() && info.size() > 0;
}

std::optional<QByteArray> ThumbnailDiskCache::load(const QString &cacheKey) const
{
    const QString path = pathFor(cacheKey);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        // Treat a zero-byte file as a corrupt entry.
        return std::nullopt;
    }
    return bytes;
}

bool ThumbnailDiskCache::store(const QString &cacheKey, const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return false;
    }
    const QString path = pathFor(cacheKey);

    // Ensure the parent directories exist.
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }

    // QSaveFile writes to a sibling temp file and renames atomically.
    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly)) {
        return false;
    }
    save.write(bytes);
    return save.commit();
}

void ThumbnailDiskCache::invalidate(const core::ContentFingerprint &fingerprint)
{
    if (!fingerprint.isValid()) {
        return;
    }
    const QString dir = m_cacheDir + QDir::separator() + fingerprint.cacheKey();
    QDir(dir).removeRecursively();
}

void ThumbnailDiskCache::trim()
{
    if (m_maxSizeBytes < 0) {
        return;
    }

    struct Entry
    {
        QString path;
        qint64 sizeBytes;
        QDateTime lastModified;
    };

    QList<Entry> entries;
    qint64 total = 0;

    QDirIterator it(m_cacheDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const qint64 size = info.size();
        entries.append({info.absoluteFilePath(), size, info.lastModified()});
        total += size;
    }

    if (total <= m_maxSizeBytes) {
        return;
    }

    // Sort ascending by last modified time: oldest entries first.
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.lastModified < b.lastModified;
    });

    for (const Entry &entry : entries) {
        if (total <= m_maxSizeBytes) {
            break;
        }
        if (QFile::remove(entry.path)) {
            total -= entry.sizeBytes;
            // Remove empty parent directories up to the cache root.
            QDir parent = QFileInfo(entry.path).dir();
            while (parent.absolutePath() != m_cacheDir && parent.isEmpty()) {
                const QString name = parent.dirName();
                parent.cdUp();
                parent.rmdir(name);
            }
        }
    }
}

qint64 ThumbnailDiskCache::totalSizeBytes() const
{
    qint64 total = 0;
    QDirIterator it(m_cacheDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

} // namespace pimio::thumbnail
