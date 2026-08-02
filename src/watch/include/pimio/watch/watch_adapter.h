#pragma once

#include "pimio/core/error.h"
#include "pimio/watch/watch_event.h"

#include <QObject>
#include <QString>

namespace pimio::watch {

/// Portable behavioral contract for a native filesystem watcher.
///
/// pimio deliberately does not hand-write three platform backends. Every
/// shipped adapter is built on a Qt cross-platform facility instead
/// (QFileSystemWatcher, which itself dispatches to inotify, FSEvents/kqueue,
/// or ReadDirectoryChangesW per platform); this interface exists so that
/// EventCoalescer, and anything built on top of it, is written against one
/// small, testable contract rather than against QFileSystemWatcher directly.
///
/// A test double can implement this interface to inject synthetic
/// WatchEvent sequences without touching a real disk; the shipped adapter
/// (QtDirectoryWatchAdapter) implements it against real directories.
class WatchAdapter : public QObject
{
    Q_OBJECT

public:
    explicit WatchAdapter(QObject *parent = nullptr);
    ~WatchAdapter() override;

    /// Starts watching \a rootPath (and, for adapters that support it,
    /// everything beneath it) for changes. Returns false and sets \a error
    /// when the root cannot be watched at all (for example, it does not
    /// exist). Partial failure to watch part of a large subtree is not a
    /// hard failure: it is reported as an Overflow event instead, since the
    /// periodic missed-event fallback exists precisely to recover from
    /// exactly that.
    virtual bool start(const QString &rootPath, core::Error *error) = 0;

    /// Stops watching. Safe to call when not currently watching.
    virtual void stop() = 0;

    virtual bool isWatching() const = 0;

signals:
    /// Emitted for every normalized change the adapter observes. May be
    /// emitted from any thread the adapter is constructed on; WatchService
    /// connects to it with a queued connection so ingestion always happens
    /// on the coalescer's own thread.
    void eventOccurred(const pimio::watch::WatchEvent &event);
};

} // namespace pimio::watch
