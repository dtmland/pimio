#pragma once

#include "pimio/core/durable_store.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <memory>

class QQmlApplicationEngine;

namespace pimio::app {

/// Composition root that wires a real, on-disk library into a running
/// application: durable storage, the SQLite projection, the scanner, the
/// thumbnail service, filesystem watching, and the browser model, all
/// exposed to QML through the "mediaLibraryModel" context property Main.qml
/// already expects.
///
/// Everything here is additive and optional. With no library path at all,
/// prepare() still registers an empty model so QML sees the same context
/// properties as a real session, while start() has no storage work to do.
///
/// Multiple library paths are supported (the --library option is
/// repeatable): every path is scanned and watched independently, but all of
/// them are indexed into one shared durable store, projection, and browser
/// model, rooted at a fixed per-user application-data location rather than
/// inside any library folder. Keeping the index outside every watched tree
/// is deliberate: writing pimio's own cache files inside a watched library
/// would make the watcher observe its own writes.
///
/// All of the pieces start() creates live for the lifetime of this object
/// and are only ever touched from the thread that constructed it, except for
/// the bounded worker threads the job dispatcher and thumbnail service
/// already manage internally; the projection database itself is only ever
/// touched from this object's own thread; see the .cpp for how job results
/// are marshalled back before it is touched again.
class LibrarySession : public QObject
{
    Q_OBJECT

public:
    explicit LibrarySession(QObject *parent = nullptr);
    ~LibrarySession() override;

    LibrarySession(const LibrarySession &) = delete;
    LibrarySession &operator=(const LibrarySession &) = delete;

    /// Registers the model, activity state, and thumbnail image provider on
    /// \a engine before QML is loaded. The requested \a libraryPaths are kept
    /// for start(), and a non-empty list immediately marks the session as
    /// starting so the first rendered frame can show progress feedback.
    void prepare(const QStringList &libraryPaths, QQmlApplicationEngine &engine);

    /// Opens durable storage (LORE-backed when this build was compiled with
    /// it, which the shipped and CI builds always are; otherwise the library
    /// stays empty and a warning is logged) and starts scanning and watching
    /// every path supplied to prepare().
    ///
    /// The initial scan of each path runs asynchronously via the job queue;
    /// opening its stores and registering recursive filesystem watches are
    /// synchronous. The application therefore calls this only after its first
    /// window frame has been presented.
    void start();

private:
    /// Pushes the current user settings (sort order, tile size, and scan
    /// batch size) into the browser model and the scanner. Called once at
    /// prepare() and again whenever one of those settings changes, from
    /// wherever it was changed.
    void applySettings();

    /// Projects a batch of records a running scan has just committed, and
    /// refreshes the grid so they are browsable while the scan continues.
    ///
    /// Always runs on this object's own thread: the scan calls back from a
    /// worker thread, which posts here rather than touching the projection
    /// (a SQLite connection belonging to this thread) or the model itself.
    void applyScanBatch(const QList<core::MediaRecord> &records, int indexedCount);

    /// Reloads the model from the projection, at most a few times a second.
    /// A scan commits far more often than a person can read, and each reload
    /// re-queries the whole ordered id list.
    void scheduleModelRefresh();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::app
