#pragma once

#include "pimio/core/file_system.h"

namespace pimio::scan {

/// Production core::FileSystem backed by real disk I/O through Qt.
///
/// This is the implementation the shipped application uses; MemoryFileSystem
/// (in tests/support) exists purely so the rest of the codebase can be tested
/// without touching a real disk. QtFileSystem itself has no test-only
/// shortcuts: every method goes through QFileInfo, QDir, QFile, QSaveFile, or
/// QStorageInfo exactly as any other Qt application would.
///
/// Symbolic links are reported as such (DirectoryEntry::isSymbolicLink) but
/// are not followed by this class; LibraryRoot::followSymlinks, consulted by
/// Scanner, decides whether a caller should traverse into one.
class QtFileSystem final : public core::FileSystem
{
public:
    bool exists(const QString &path) const override;
    bool isDirectory(const QString &path) const override;

    QList<core::DirectoryEntry> listDirectory(const QString &path,
                                              core::Error *error) const override;

    core::FileIdentity identify(const QString &path, core::Error *error) const override;

    QByteArray readAll(const QString &path, core::Error *error) const override;

    bool writeAtomically(const QString &path, const QByteArray &contents,
                         core::Error *error) override;

    bool remove(const QString &path, core::Error *error) override;
    bool makeDirectories(const QString &path, core::Error *error) override;

    qint64 availableSpaceBytes(const QString &path) const override;
};

} // namespace pimio::scan
