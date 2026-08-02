#pragma once

#include "pimio/watch/watch_adapter.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QString>

#include <memory>

namespace pimio::watch {

/// Recursive directory watcher built on QFileSystemWatcher.
///
/// Directory notifications do not say which entry changed, so this adapter
/// keeps a snapshot of each watched directory and diffs it whenever
/// QFileSystemWatcher fires. Files are watched as well so in-place content
/// changes produce Modified events.
///
/// New directories discovered by a diff are watched recursively so the whole
/// subtree stays covered; directories that disappear are unwatched. Because
/// QFileSystemWatcher enforces a per-process watch-descriptor limit on some
/// platforms (inotify's fs.inotify.max_user_watches, in particular), a
/// directory that cannot be added to the watcher is reported as an Overflow
/// event for that subtree rather than silently going unwatched: the periodic
/// missed-event fallback (see EventCoalescer::isPeriodicFallbackDue) is what
/// eventually recovers from that.
///
/// All Qt objects owned by this adapter are only ever touched from the
/// thread that constructed it; eventOccurred() is therefore always emitted
/// from that same thread.
class QtDirectoryWatchAdapter final : public WatchAdapter
{
    Q_OBJECT

public:
    explicit QtDirectoryWatchAdapter(QObject *parent = nullptr);
    ~QtDirectoryWatchAdapter() override;

    bool start(const QString &rootPath, core::Error *error) override;
    void stop() override;
    bool isWatching() const override;

private slots:
    void onDirectoryChanged(const QString &path);
    void onFileChanged(const QString &path);

private:
    void watchFile(const QString &path);
    void watchRecursively(const QString &dirPath);
    void unwatchRecursively(const QString &dirPath);
    QSet<QString> snapshotEntries(const QString &dirPath) const;

    std::unique_ptr<QFileSystemWatcher> m_watcher;
    QString m_root;
    bool m_watching = false;

    /// Direct-child entry names last observed for each watched directory.
    QHash<QString, QSet<QString>> m_knownEntries;
};

} // namespace pimio::watch
