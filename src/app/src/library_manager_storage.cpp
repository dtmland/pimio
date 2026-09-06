#include "library_manager_storage.h"

#include "pimio/projection/projection_database.h"

#ifdef PIMIO_HAVE_LORE
#include "pimio/lore/lore_durable_store.h"
#endif

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <limits>

namespace pimio::app::library_manager_storage {
namespace {

constexpr quint32 kArchiveMagic = 0x50494d42; // PIMB
constexpr quint32 kArchiveVersion = 1;
constexpr quint32 kMaximumArchiveEntries = 1000000;

QStringList archiveFiles(const QString &location, core::Error *error)
{
    const QString storePath = storePathFor(location);
    QStringList files;
    QDirIterator iterator(storePath, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("A Library cannot contain symbolic links."));
            return {};
        }
        const QString relative = QDir(location).relativeFilePath(path);
        if (relative != QStringLiteral("store/.pimio-writer.lock")) {
            files.append(relative);
        }
    }
    files.sort();
    return files;
}

bool isSafeArchivePath(const QString &relative)
{
    return !relative.isEmpty() && !QDir::isAbsolutePath(relative)
           && QDir::cleanPath(relative) == relative && !relative.startsWith(QStringLiteral("../"))
           && relative.startsWith(QStringLiteral("store/"));
}

QString indexDirectoryFor(const QString &libraryId)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/library-indexes/") + libraryId;
}

QString thumbnailDirectoryFor(const QString &libraryId)
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/libraries/") + libraryId;
}

} // namespace

void assignError(core::Error *target, core::ErrorCode code, const QString &message)
{
    if (target) {
        *target = core::Error(code, message);
    }
}

QString normalizedLocation(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString storePathFor(const QString &location)
{
    return QDir(location).filePath(QStringLiteral("store"));
}

bool copyTree(const QString &source, const QString &destination, core::Error *error)
{
    if (!QDir().mkpath(destination)) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    QObject::tr("Could not create %1.").arg(destination));
        return false;
    }
    QDirIterator iterator(source, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = QDir(source).relativeFilePath(sourcePath);
        if (relative == QStringLiteral("store/.pimio-writer.lock")) {
            continue;
        }
        const QString targetPath = QDir(destination).filePath(relative);
        if (info.isSymLink()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("A Library cannot contain symbolic links: %1").arg(relative));
            return false;
        }
        if (info.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                assignError(error, core::ErrorCode::PermissionDenied,
                            QObject::tr("Could not create %1.").arg(targetPath));
                return false;
            }
        } else if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())
                   || !QFile::copy(sourcePath, targetPath)) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not copy %1.").arg(relative));
            return false;
        }
    }
    return true;
}

bool writeArchive(const QString &location, const QString &archivePath, core::Error *error)
{
    const QStringList files = archiveFiles(location, error);
    if (files.isEmpty() && error && error->isError()) {
        return false;
    }
    if (files.size() > static_cast<qsizetype>(kMaximumArchiveEntries)) {
        assignError(error, core::ErrorCode::Conflict,
                    QObject::tr("The Library contains too many files to archive."));
        return false;
    }

    QSaveFile output(archivePath);
    if (!QDir().mkpath(QFileInfo(archivePath).absolutePath())
        || !output.open(QIODevice::WriteOnly)) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    QObject::tr("Could not create the backup archive."));
        return false;
    }
    QDataStream stream(&output);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kArchiveMagic << kArchiveVersion << static_cast<quint32>(files.size());

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (const QString &relative : files) {
        QFile input(QDir(location).filePath(relative));
        if (!input.open(QIODevice::ReadOnly)) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not read %1 for backup.").arg(relative));
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!input.atEnd()) {
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count < 0) {
                assignError(error, core::ErrorCode::Interrupted,
                            QObject::tr("Could not read %1 for backup.").arg(relative));
                return false;
            }
            hash.addData(QByteArrayView(buffer.constData(), count));
        }
        stream << relative << static_cast<quint64>(input.size()) << hash.result();
        input.seek(0);
        QCryptographicHash streamedHash(QCryptographicHash::Sha256);
        while (!input.atEnd()) {
            const qint64 count = input.read(buffer.data(), buffer.size());
            if (count <= 0 || stream.writeRawData(buffer.constData(), static_cast<int>(count))
                                      != count) {
                assignError(error, core::ErrorCode::OutOfSpace,
                            QObject::tr("Could not finish the backup archive."));
                return false;
            }
            streamedHash.addData(QByteArrayView(buffer.constData(), count));
        }
        if (streamedHash.result() != hash.result()) {
            assignError(error, core::ErrorCode::Interrupted,
                        QObject::tr("A Library file changed while it was being backed up."));
            return false;
        }
    }
    if (stream.status() != QDataStream::Ok || !output.commit()) {
        assignError(error, core::ErrorCode::OutOfSpace,
                    QObject::tr("Could not finish the backup archive."));
        return false;
    }
    return true;
}

bool extractArchive(const QString &archivePath, const QString &destination, core::Error *error)
{
    QFile input(archivePath);
    if (!input.open(QIODevice::ReadOnly)) {
        assignError(error, core::ErrorCode::NotFound,
                    QObject::tr("Could not open the backup archive."));
        return false;
    }
    QDataStream stream(&input);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint32 version = 0;
    quint32 count = 0;
    stream >> magic >> version >> count;
    if (magic != kArchiveMagic || version != kArchiveVersion
        || count > kMaximumArchiveEntries) {
        assignError(error, core::ErrorCode::CorruptData,
                    QObject::tr("The backup archive is invalid or unsupported."));
        return false;
    }

    QSet<QString> seen;
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (quint32 index = 0; index < count; ++index) {
        QString relative;
        quint64 size = 0;
        QByteArray expectedHash;
        stream >> relative >> size >> expectedHash;
        if (stream.status() != QDataStream::Ok || !isSafeArchivePath(relative)
            || seen.contains(relative) || expectedHash.size() != 32
            || size > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("The backup archive contains an invalid entry."));
            return false;
        }
        seen.insert(relative);
        const QString targetPath = QDir(destination).filePath(relative);
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not create the restore destination."));
            return false;
        }
        QSaveFile output(targetPath);
        if (!output.open(QIODevice::WriteOnly)) {
            assignError(error, core::ErrorCode::PermissionDenied,
                        QObject::tr("Could not restore %1.").arg(relative));
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        quint64 remaining = size;
        while (remaining > 0) {
            const int requested = static_cast<int>(
                    std::min<quint64>(remaining, static_cast<quint64>(buffer.size())));
            const int read = stream.readRawData(buffer.data(), requested);
            if (read != requested || output.write(buffer.constData(), read) != read) {
                assignError(error, core::ErrorCode::CorruptData,
                            QObject::tr("The backup archive ended unexpectedly."));
                return false;
            }
            hash.addData(QByteArrayView(buffer.constData(), read));
            remaining -= static_cast<quint64>(read);
        }
        if (hash.result() != expectedHash || !output.commit()) {
            assignError(error, core::ErrorCode::CorruptData,
                        QObject::tr("Backup verification failed for %1.").arg(relative));
            return false;
        }
    }
    if (stream.status() != QDataStream::Ok || !input.atEnd()) {
        assignError(error, core::ErrorCode::CorruptData,
                    QObject::tr("The backup archive contains unexpected data."));
        return false;
    }
    return true;
}

#ifdef PIMIO_HAVE_LORE
std::optional<LibraryInfo> inspectLibrary(const QString &location, core::Error *error)
{
    const QString normalized = normalizedLocation(location);
    if (!QFileInfo(normalized).isDir() || !QFileInfo(storePathFor(normalized)).isDir()) {
        assignError(error, core::ErrorCode::NotFound,
                    QObject::tr("No Library exists at %1.").arg(normalized));
        return std::nullopt;
    }
    lore::LoreDurableStore store(storePathFor(normalized));
    if (!store.open(error)) {
        return std::nullopt;
    }
    const auto descriptor = store.libraryDescriptor(error);
    store.close();
    if (!descriptor) {
        return std::nullopt;
    }
    return LibraryInfo{descriptor->id, descriptor->name, normalized};
}

bool rebuildDerivedState(const LibraryInfo &library, core::Error *error)
{
    const QString indexPath = indexDirectoryFor(library.id);
    QDir(indexPath).removeRecursively();
    QDir(thumbnailDirectoryFor(library.id)).removeRecursively();
    if (!QDir().mkpath(indexPath)) {
        assignError(error, core::ErrorCode::PermissionDenied,
                    QObject::tr("Could not create derived Library storage."));
        return false;
    }

    lore::LoreDurableStore store(storePathFor(library.location));
    if (!store.open(error)) {
        return false;
    }
    projection::ProjectionDatabase projection;
    if (!projection.open(QDir(indexPath).filePath(QStringLiteral("projection.sqlite3")), error)
        || !projection.rebuildFrom(store, error)) {
        return false;
    }
    store.close();
    return true;
}
#endif

} // namespace pimio::app::library_manager_storage
