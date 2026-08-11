#pragma once

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
/// start() is simply never called, and startup behaves exactly as it always
/// has: an empty QQmlApplicationEngine loading Main.qml against whatever
/// context properties a test itself set up, which is what keeps `pimio` and
/// `pimio --self-check` working with no arguments.
///
/// Multiple library paths are supported (the --library option is
/// repeatable): every path is scanned and watched independently, but all of
/// them are indexed into one shared durable store, projection, and browser
/// model, rooted at a fixed per-user application-data location rather than
/// inside any library folder. Keeping the index outside every watched tree
/// is deliberate: writing pimio's own cache files inside a watched library
/// would make the watcher observe its own writes.
///
/// All of the pieces start()s creates live for the lifetime of this object
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

    /// Opens durable storage (LORE-backed when this build was compiled with
    /// it, which the shipped and CI builds always are; otherwise the library
    /// stays empty and a warning is logged, exactly like a missing
    /// --library) and starts scanning and watching every path in \a
    /// libraryPaths. Registers "mediaLibraryModel" on \a engine's root
    /// context and an "thumbnail" image provider before returning, so both
    /// exist before the caller calls engine.load() — no QML binding ever
    /// observes them appearing.
    ///
    /// The initial scan of each path runs asynchronously via the job queue;
    /// this call itself never blocks on disk I/O beyond opening the (small,
    /// local) store, projection, and job-queue files.
    void start(const QStringList &libraryPaths, QQmlApplicationEngine &engine);

private:
    /// Pushes the current user settings (sort order and tile size) into the
    /// browser model. Called once at start() and again whenever one of those
    /// settings changes, from wherever it was changed.
    void applySettings();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::app
