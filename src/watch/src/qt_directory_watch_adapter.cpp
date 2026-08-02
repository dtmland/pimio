#include "pimio/watch/qt_directory_watch_adapter.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace pimio::watch {

QtDirectoryWatchAdapter::QtDirectoryWatchAdapter(QObject *parent)
    : WatchAdapter(parent)
{
}

QtDirectoryWatchAdapter::~QtDirectoryWatchAdapter()
{
    stop();
}

QSet<QString> QtDirectoryWatchAdapter::snapshotEntries(const QString &dirPath) const
{
    QSet<QString> entries;
    const QStringList names = QDir(dirPath).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString &name : names) {
        entries.insert(name);
    }
    return entries;
}

void QtDirectoryWatchAdapter::watchFile(const QString &path)
{
    if (m_watcher->files().contains(path)) {
        return;
    }
    if (!m_watcher->addPath(path)) {
        WatchEvent event;
        event.kind = WatchEventKind::Overflow;
        event.path = path;
        event.observedAt = QDateTime::currentDateTimeUtc();
        emit eventOccurred(event);
    }
}

void QtDirectoryWatchAdapter::watchRecursively(const QString &dirPath)
{
    if (m_knownEntries.contains(dirPath)) {
        return;
    }
    if (!m_watcher->addPath(dirPath)) {
        // Most commonly a platform watch-descriptor limit (inotify's
        // fs.inotify.max_user_watches). We can no longer see changes under
        // this directory through the watcher, so say so explicitly rather
        // than silently missing everything beneath it.
        WatchEvent event;
        event.kind = WatchEventKind::Overflow;
        event.path = dirPath;
        event.observedAt = QDateTime::currentDateTimeUtc();
        emit eventOccurred(event);
        return;
    }

    const QSet<QString> entries = snapshotEntries(dirPath);
    m_knownEntries.insert(dirPath, entries);

    for (const QString &name : entries) {
        const QString childPath = dirPath + QLatin1Char('/') + name;
        const QFileInfo info(childPath);
        if (info.isDir() && !info.isSymLink()) {
            watchRecursively(childPath);
        } else {
            watchFile(childPath);
        }
    }
}

void QtDirectoryWatchAdapter::unwatchRecursively(const QString &dirPath)
{
    QStringList toRemove;
    const QString prefix = dirPath + QLatin1Char('/');
    for (auto it = m_knownEntries.constBegin(); it != m_knownEntries.constEnd(); ++it) {
        if (it.key() == dirPath || it.key().startsWith(prefix)) {
            toRemove.append(it.key());
        }
    }
    for (const QString &path : toRemove) {
        m_watcher->removePath(path);
        m_knownEntries.remove(path);
    }
}

bool QtDirectoryWatchAdapter::start(const QString &rootPath, core::Error *error)
{
    stop();

    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (error) {
            *error = core::Error(core::ErrorCode::NotFound,
                                 QStringLiteral("Cannot watch a path that is not a directory: %1")
                                         .arg(rootPath));
        }
        return false;
    }

    m_watcher = std::make_unique<QFileSystemWatcher>();
    connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, this,
            &QtDirectoryWatchAdapter::onDirectoryChanged);
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, this,
            &QtDirectoryWatchAdapter::onFileChanged);

    m_root = rootInfo.absoluteFilePath();
    watchRecursively(m_root);
    m_watching = true;
    return true;
}

void QtDirectoryWatchAdapter::stop()
{
    if (m_watcher) {
        m_watcher->disconnect(this);
        m_watcher.reset();
    }
    m_knownEntries.clear();
    m_root.clear();
    m_watching = false;
}

bool QtDirectoryWatchAdapter::isWatching() const
{
    return m_watching;
}

void QtDirectoryWatchAdapter::onDirectoryChanged(const QString &path)
{
    if (!m_watcher) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QFileInfo pathInfo(path);

    if (!pathInfo.exists() || !pathInfo.isDir()) {
        // Qt already stops watching a path once it is removed; clean up our
        // own bookkeeping to match and report the removal.
        const bool wasRoot = (path == m_root);
        unwatchRecursively(path);

        WatchEvent event;
        event.kind = WatchEventKind::Removed;
        event.path = path;
        event.observedAt = now;
        emit eventOccurred(event);

        if (wasRoot) {
            m_watching = false;
        }
        return;
    }

    const QSet<QString> previous = m_knownEntries.value(path);
    const QSet<QString> current = snapshotEntries(path);
    m_knownEntries.insert(path, current);

    const QSet<QString> removedNames = previous - current;
    const QSet<QString> addedNames = current - previous;

    for (const QString &name : removedNames) {
        const QString childPath = path + QLatin1Char('/') + name;
        unwatchRecursively(childPath);

        WatchEvent event;
        event.kind = WatchEventKind::Removed;
        event.path = childPath;
        event.observedAt = now;
        emit eventOccurred(event);
    }

    for (const QString &name : addedNames) {
        const QString childPath = path + QLatin1Char('/') + name;

        WatchEvent event;
        event.kind = WatchEventKind::Created;
        event.path = childPath;
        event.observedAt = now;
        emit eventOccurred(event);

        const QFileInfo childInfo(childPath);
        if (childInfo.isDir() && !childInfo.isSymLink()) {
            watchRecursively(childPath);
        } else {
            watchFile(childPath);
        }
    }
}

void QtDirectoryWatchAdapter::onFileChanged(const QString &path)
{
    if (!m_watcher) {
        return;
    }

    WatchEvent event;
    event.kind = QFileInfo::exists(path) ? WatchEventKind::Modified : WatchEventKind::Removed;
    event.path = path;
    event.observedAt = QDateTime::currentDateTimeUtc();
    emit eventOccurred(event);

    // Some backends stop watching after an atomic replacement. If the path
    // still exists, restore the watch so subsequent edits are observed too.
    if (QFileInfo::exists(path)) {
        watchFile(path);
    }
}

} // namespace pimio::watch
