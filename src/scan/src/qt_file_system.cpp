#include "pimio/scan/qt_file_system.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#endif

namespace pimio::scan {

namespace {

/// Fills \a volumeId and \a fileId with the platform's stable identity for
/// the file at \a absolutePath, when the platform can provide one.
///
/// On POSIX this is the device id and inode number, which stay the same
/// across a rename on the same volume. On Windows it is the volume serial
/// number and file index reported by the filesystem. Neither field is set
/// when the platform call fails (for example, a broken symbolic link), which
/// FileIdentity::sameFileAs treats as "unknown" rather than a false match.
void platformIdentity(const QString &absolutePath, QString *volumeId, QString *fileId)
{
#ifdef Q_OS_WIN
    const std::wstring nativePath = absolutePath.toStdWString();
    HANDLE handle = CreateFileW(nativePath.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(handle, &info)) {
        *volumeId = QString::number(info.dwVolumeSerialNumber);
        const quint64 index =
                (quint64(info.nFileIndexHigh) << 32) | quint64(info.nFileIndexLow);
        *fileId = QString::number(index);
    }
    CloseHandle(handle);
#else
    struct stat statBuffer{};
    const QByteArray encoded = QFile::encodeName(absolutePath);
    if (::stat(encoded.constData(), &statBuffer) == 0) {
        *volumeId = QString::number(static_cast<qulonglong>(statBuffer.st_dev));
        *fileId = QString::number(static_cast<qulonglong>(statBuffer.st_ino));
    }
#endif
}

} // namespace

bool QtFileSystem::exists(const QString &path) const
{
    return QFileInfo::exists(path);
}

bool QtFileSystem::isDirectory(const QString &path) const
{
    return QFileInfo(path).isDir();
}

QList<core::DirectoryEntry> QtFileSystem::listDirectory(const QString &path,
                                                        core::Error *error) const
{
    const QFileInfo dirInfo(path);
    if (!dirInfo.exists()) {
        if (error) {
            *error = core::Error(core::ErrorCode::NotFound,
                                 QStringLiteral("Directory does not exist: %1").arg(path));
        }
        return {};
    }
    if (!dirInfo.isDir()) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("Not a directory: %1").arg(path));
        }
        return {};
    }
    if (!dirInfo.isReadable()) {
        if (error) {
            *error = core::Error(core::ErrorCode::PermissionDenied,
                                 QStringLiteral("Cannot list directory: %1").arg(path));
        }
        return {};
    }

    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);

    QList<core::DirectoryEntry> result;
    result.reserve(entries.size());
    for (const QFileInfo &info : entries) {
        core::DirectoryEntry entry;
        entry.absolutePath = info.absoluteFilePath();
        entry.fileName = info.fileName();
        entry.isSymbolicLink = info.isSymbolicLink();
        entry.isDirectory = info.isDir();

        // A per-entry identity failure (for example a broken symbolic link
        // that no longer resolves) is not fatal to the listing itself;
        // Scanner treats an invalid identity as a per-file problem.
        core::Error identityError;
        entry.identity = identify(entry.absolutePath, &identityError);

        result.append(entry);
    }
    return result;
}

core::FileIdentity QtFileSystem::identify(const QString &path, core::Error *error) const
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        if (error) {
            *error = core::Error(core::ErrorCode::NotFound,
                                 QStringLiteral("File does not exist: %1").arg(path));
        }
        return {};
    }

    core::FileIdentity identity;
    identity.absolutePath = info.absoluteFilePath();
    identity.sizeBytes = info.exists() ? info.size() : 0;
    identity.lastModified = info.lastModified();
    platformIdentity(info.absoluteFilePath(), &identity.volumeId, &identity.fileId);
    return identity;
}

QByteArray QtFileSystem::readAll(const QString &path, core::Error *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        const core::ErrorCode code = file.exists() ? core::ErrorCode::PermissionDenied
                                                   : core::ErrorCode::NotFound;
        if (error) {
            *error = core::Error(code,
                                 QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        }
        return {};
    }
    return file.readAll();
}

bool QtFileSystem::writeAtomically(const QString &path, const QByteArray &contents,
                                   core::Error *error)
{
    // QSaveFile writes to a sibling temporary file and renames it over the
    // target only once every byte has been flushed, so an interrupted write
    // never leaves a partially written file in place.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = core::Error(core::ErrorCode::PermissionDenied,
                                 QStringLiteral("Cannot open %1 for writing: %2")
                                         .arg(path, file.errorString()));
        }
        return false;
    }
    if (file.write(contents) != contents.size()) {
        if (error) {
            *error = core::Error(core::ErrorCode::OutOfSpace,
                                 QStringLiteral("Failed writing %1: %2")
                                         .arg(path, file.errorString()));
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("Failed to commit %1: %2")
                                         .arg(path, file.errorString()));
        }
        return false;
    }
    return true;
}

bool QtFileSystem::remove(const QString &path, core::Error *error)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        // Idempotent: nothing to remove is not a failure.
        return true;
    }

    const bool ok = info.isDir() && !info.isSymLink() ? QDir(path).rmdir(path) : QFile::remove(path);
    if (!ok && error) {
        *error = core::Error(core::ErrorCode::PermissionDenied,
                             QStringLiteral("Cannot remove %1").arg(path));
    }
    return ok;
}

bool QtFileSystem::makeDirectories(const QString &path, core::Error *error)
{
    if (QDir(path).mkpath(QStringLiteral("."))) {
        return true;
    }
    if (error) {
        *error = core::Error(core::ErrorCode::PermissionDenied,
                             QStringLiteral("Cannot create directory: %1").arg(path));
    }
    return false;
}

qint64 QtFileSystem::availableSpaceBytes(const QString &path) const
{
    const QStorageInfo info(path);
    if (!info.isValid()) {
        return -1;
    }
    return info.bytesAvailable();
}

} // namespace pimio::scan
