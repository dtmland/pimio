#include "pimio/testing/memory_file_system.h"

#include <QFileInfo>

#include <algorithm>

namespace pimio::testing {
namespace {

core::Error makeError(core::ErrorCode code, const QString &path)
{
    core::Error error(code, QStringLiteral("%1: %2").arg(core::toString(code), path));
    QJsonObject context;
    context.insert(QStringLiteral("path"), path);
    error.setContext(context);
    return error;
}

void setError(core::Error *target, core::ErrorCode code, const QString &path)
{
    if (target) {
        *target = makeError(code, path);
    }
}

} // namespace

MemoryFileSystem::MemoryFileSystem()
{
    addDirectory(QStringLiteral("/"));
}

QString MemoryFileSystem::parentOf(const QString &path)
{
    const int index = path.lastIndexOf(QLatin1Char('/'));
    if (index <= 0) {
        return QStringLiteral("/");
    }
    return path.left(index);
}

void MemoryFileSystem::addDirectory(const QString &path)
{
    if (path != QLatin1String("/")) {
        addDirectory(parentOf(path));
    }
    if (m_nodes.contains(path)) {
        return;
    }
    Node node;
    node.isDirectory = true;
    node.lastModified = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
    node.fileId = QString::number(m_nextFileId++);
    m_nodes.insert(path, node);
}

void MemoryFileSystem::addFile(const QString &path, const QByteArray &contents,
                               const QDateTime &lastModified)
{
    addDirectory(parentOf(path));
    Node node = m_nodes.value(path);
    if (node.fileId.isEmpty()) {
        node.fileId = QString::number(m_nextFileId++);
    }
    node.isDirectory = false;
    node.isSymbolicLink = false;
    node.contents = contents;
    node.lastModified = lastModified.toUTC();
    m_nodes.insert(path, node);
}

void MemoryFileSystem::addSymbolicLink(const QString &path, const QString &target)
{
    addDirectory(parentOf(path));
    Node node;
    node.isSymbolicLink = true;
    node.linkTarget = target;
    node.lastModified = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
    node.fileId = QString::number(m_nextFileId++);
    m_nodes.insert(path, node);
}

void MemoryFileSystem::injectFailure(const QString &path, core::ErrorCode code)
{
    m_failures.insert(path, code);
}

void MemoryFileSystem::clearFailures()
{
    m_failures.clear();
}

void MemoryFileSystem::setAvailableSpaceBytes(qint64 bytes)
{
    m_availableSpaceBytes = bytes;
}

int MemoryFileSystem::writeCount() const
{
    return m_writeCount;
}

bool MemoryFileSystem::checkFailure(const QString &path, core::Error *error) const
{
    const auto it = m_failures.constFind(path);
    if (it == m_failures.constEnd()) {
        return false;
    }
    setError(error, it.value(), path);
    return true;
}

bool MemoryFileSystem::exists(const QString &path) const
{
    return m_nodes.contains(path);
}

bool MemoryFileSystem::isDirectory(const QString &path) const
{
    return m_nodes.value(path).isDirectory;
}

QList<core::DirectoryEntry> MemoryFileSystem::listDirectory(const QString &path,
                                                            core::Error *error) const
{
    if (checkFailure(path, error)) {
        return {};
    }
    if (!m_nodes.contains(path)) {
        setError(error, core::ErrorCode::NotFound, path);
        return {};
    }
    if (!m_nodes.value(path).isDirectory) {
        setError(error, core::ErrorCode::UnsupportedMedia, path);
        return {};
    }

    QList<core::DirectoryEntry> entries;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        if (it.key() == path || parentOf(it.key()) != path) {
            continue;
        }
        core::DirectoryEntry entry;
        entry.absolutePath = it.key();
        entry.fileName = QFileInfo(it.key()).fileName();
        entry.isDirectory = it.value().isDirectory;
        entry.isSymbolicLink = it.value().isSymbolicLink;
        entry.identity = identify(it.key(), nullptr);
        entries.append(entry);
    }

    // Deterministic order regardless of hash iteration order.
    std::sort(entries.begin(), entries.end(),
              [](const core::DirectoryEntry &lhs, const core::DirectoryEntry &rhs) {
                  return lhs.absolutePath < rhs.absolutePath;
              });
    return entries;
}

core::FileIdentity MemoryFileSystem::identify(const QString &path, core::Error *error) const
{
    if (checkFailure(path, error)) {
        return {};
    }
    const auto it = m_nodes.constFind(path);
    if (it == m_nodes.constEnd()) {
        setError(error, core::ErrorCode::NotFound, path);
        return {};
    }

    core::FileIdentity identity;
    identity.absolutePath = path;
    identity.volumeId = QStringLiteral("memory");
    identity.fileId = it.value().fileId;
    identity.sizeBytes = it.value().isDirectory ? 0 : it.value().contents.size();
    identity.lastModified = it.value().lastModified;
    return identity;
}

QByteArray MemoryFileSystem::readAll(const QString &path, core::Error *error) const
{
    if (checkFailure(path, error)) {
        return {};
    }
    const auto it = m_nodes.constFind(path);
    if (it == m_nodes.constEnd()) {
        setError(error, core::ErrorCode::NotFound, path);
        return {};
    }
    if (it.value().isDirectory) {
        setError(error, core::ErrorCode::UnsupportedMedia, path);
        return {};
    }
    return it.value().contents;
}

bool MemoryFileSystem::writeAtomically(const QString &path, const QByteArray &contents,
                                       core::Error *error)
{
    if (checkFailure(path, error)) {
        return false;
    }
    if (m_availableSpaceBytes >= 0 && contents.size() > m_availableSpaceBytes) {
        setError(error, core::ErrorCode::OutOfSpace, path);
        return false;
    }

    const QDateTime previousModified = m_nodes.value(path).lastModified;
    addFile(path, contents,
            previousModified.isValid() ? previousModified.addSecs(1)
                                       : QDateTime::fromSecsSinceEpoch(0, Qt::UTC));
    ++m_writeCount;
    return true;
}

bool MemoryFileSystem::remove(const QString &path, core::Error *error)
{
    if (checkFailure(path, error)) {
        return false;
    }
    if (m_nodes.remove(path) == 0) {
        setError(error, core::ErrorCode::NotFound, path);
        return false;
    }
    return true;
}

bool MemoryFileSystem::makeDirectories(const QString &path, core::Error *error)
{
    if (checkFailure(path, error)) {
        return false;
    }
    addDirectory(path);
    return true;
}

qint64 MemoryFileSystem::availableSpaceBytes(const QString &) const
{
    return m_availableSpaceBytes;
}

} // namespace pimio::testing
