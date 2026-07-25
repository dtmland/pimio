#pragma once

#include "pimio/core/error.h"
#include "pimio/core/types.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace pimio::core {

/// One filesystem entry as seen by a scan.
struct DirectoryEntry
{
    QString absolutePath;
    QString fileName;
    bool isDirectory = false;
    bool isSymbolicLink = false;
    FileIdentity identity;
};

/// Filesystem boundary used by the core.
///
/// The core never touches QFile or std::filesystem directly. Tests substitute
/// an in-memory implementation so that permission failures, disappearing
/// files, and out-of-space conditions can be injected deterministically.
class FileSystem
{
public:
    virtual ~FileSystem();

    virtual bool exists(const QString &path) const = 0;
    virtual bool isDirectory(const QString &path) const = 0;

    /// Non-recursive listing. Traversal order is defined by the caller so that
    /// scans are deterministic regardless of platform enumeration order.
    virtual QList<DirectoryEntry> listDirectory(const QString &path, Error *error) const = 0;

    virtual FileIdentity identify(const QString &path, Error *error) const = 0;

    virtual QByteArray readAll(const QString &path, Error *error) const = 0;

    /// Writes \a contents to \a path so that an interrupted write never leaves
    /// a partially written file in place. Implementations write to a temporary
    /// file in the same directory and rename it over the target.
    virtual bool writeAtomically(const QString &path, const QByteArray &contents,
                                 Error *error) = 0;

    virtual bool remove(const QString &path, Error *error) = 0;
    virtual bool makeDirectories(const QString &path, Error *error) = 0;

    /// Free space in bytes on the volume containing \a path, or -1 when it
    /// cannot be determined.
    virtual qint64 availableSpaceBytes(const QString &path) const = 0;
};

} // namespace pimio::core
