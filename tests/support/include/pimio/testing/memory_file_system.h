#pragma once

#include "pimio/core/file_system.h"

#include <QHash>
#include <QSet>

namespace pimio::testing {

/// In-memory FileSystem used by contract tests.
///
/// It supports deterministic failure injection so that permission loss,
/// out-of-space, and disappearing files can be tested without root access or
/// a real disk.
class MemoryFileSystem final : public core::FileSystem
{
public:
    MemoryFileSystem();

    // Test setup helpers.
    void addFile(const QString &path, const QByteArray &contents,
                 const QDateTime &lastModified = QDateTime::currentDateTimeUtc());
    void addDirectory(const QString &path);
    void addSymbolicLink(const QString &path, const QString &target);

    /// Every subsequent operation touching \a path fails with \a code.
    void injectFailure(const QString &path, core::ErrorCode code);
    void clearFailures();

    /// Sets the reported free space. -1 means "unknown".
    void setAvailableSpaceBytes(qint64 bytes);

    /// Number of successful atomic writes. Used to assert that a failed write
    /// never replaced the previous file.
    int writeCount() const;

    // FileSystem
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

private:
    struct Node
    {
        bool isDirectory = false;
        bool isSymbolicLink = false;
        QString linkTarget;
        QByteArray contents;
        QDateTime lastModified;
        QString fileId;
    };

    bool checkFailure(const QString &path, core::Error *error) const;
    static QString parentOf(const QString &path);

    QHash<QString, Node> m_nodes;
    QHash<QString, core::ErrorCode> m_failures;
    qint64 m_availableSpaceBytes = -1;
    int m_writeCount = 0;
    quint64 m_nextFileId = 1;
};

} // namespace pimio::testing
