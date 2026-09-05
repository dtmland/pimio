#pragma once

#include "pimio/core/durable_store.h"

#include <QString>

#include <memory>

namespace pimio::lore {

/// Absolute path of the LORE shared library the process will load.
///
/// The build records the path of the acquired artifact; the
/// `PIMIO_LORE_LIBRARY` environment variable overrides it so a packaged build
/// can point at its own copy.
QString defaultLibraryPath();

/// Version string reported by the loaded LORE library, or an empty string when
/// the library could not be loaded.
QString loadedLibraryVersion();

/// A durable store backed by a local, offline LORE repository.
///
/// Layout under \c storePath:
///
/// \code
///   repository/          LORE repository: its checkout plus its own state
///     records/           one JSON file per media record, sharded one level
///   staging/             records staged by pimio but not yet committed
/// \endcode
///
/// The checkout is owned exclusively by pimio. Staged records are deliberately
/// kept outside it so that the checkout only ever holds committed state, which
/// is what makes \c load() unable to report an uncommitted change as
/// committed. See docs/decisions/0001-lore-durable-store.md.
///
/// The library is loaded dynamically. When it is missing the store reports
/// itself unavailable and every operation fails with
/// \c ErrorCode::StorageUnavailable; nothing throws and the process still
/// starts.
class LoreDurableStore final : public core::DurableStore
{
public:
    explicit LoreDurableStore(QString storePath);
    ~LoreDurableStore() override;

    LoreDurableStore(const LoreDurableStore &) = delete;
    LoreDurableStore &operator=(const LoreDurableStore &) = delete;

    /// Opens, and if necessary creates, the repository. Also runs recovery for
    /// a commit that was interrupted before it completed.
    ///
    /// Safe to call more than once. Returns false and fills \a error when the
    /// library or the repository cannot be opened.
    bool open(core::Error *error);

    /// Releases LORE's cached handles for this repository without deleting
    /// anything. A later open() reopens it.
    void close();

    /// Rebuilds the checkout from the committed revision, discarding anything
    /// in it that LORE does not vouch for.
    ///
    /// The checkout is derived state: LORE's own store is the ground truth.
    /// Deleting or corrupting the checkout must therefore be recoverable, and
    /// this is the primitive that recovers it. Staged records live outside the
    /// checkout and are untouched.
    bool restoreFromDurableState(core::Error *error);

    /// True when open() had to repair an interrupted write to get the store
    /// open. Callers surface this so a silent recovery never looks like a
    /// clean start.
    bool repairedInterruptedWriteOnOpen() const;

    /// True when the repository cannot be opened because a killed process left
    /// a zero-length pending marker in LORE's local store.
    ///
    /// This is a defect in the pinned LORE release, not a pimio invariant: the
    /// committed data is intact, but LORE refuses to open the store until the
    /// empty marker is removed. See docs/decisions/0001-lore-durable-store.md.
    bool needsRepairAfterInterruptedWrite() const;

    /// Removes zero-length pending markers left behind by a killed process.
    ///
    /// open() runs this itself once it holds the single-writer lock, because
    /// holding that lock is what proves a marker is leftover rather than a
    /// write in flight. It is public so a diagnostic tool can run it too. It
    /// removes only empty markers, which by construction record nothing, so no
    /// committed data is discarded. Returns false and reports an error when
    /// nothing was repairable or a marker could not be removed.
    bool repairAfterInterruptedWrite(core::Error *error);

    const QString &storePath() const;
    QString repositoryPath() const;

    /// Registers the local repository at \a remoteUrl, verifies that the
    /// remote has the same immutable repository identity, attaches it
    /// atomically, and pushes the current branch.
    ///
    /// A failed initial push may require server-side repair with LORE 0.9.0.
    /// The remote remains attached so a future LORE release can retry it.
    bool promoteToServer(const QString &remoteUrl, core::Error *error);

    // DurableStore
    bool isAvailable() const override;
    bool createLibrary(const QString &name, core::Error *error) override;
    std::optional<core::LibraryDescriptor> libraryDescriptor(core::Error *error) const override;
    bool stage(const core::MediaRecord &record, core::Error *error) override;
    std::optional<core::Checkpoint> commit(const QString &message, core::Error *error) override;
    bool discardStaged(core::Error *error) override;
    bool hasStagedChanges() const override;
    bool remove(const core::MediaId &id, core::Error *error) override;
    std::optional<core::MediaRecord> load(const core::MediaId &id,
                                          core::Error *error) const override;
    QList<core::MediaId> listIds(core::Error *error) const override;
    QList<core::Checkpoint> history(int limit, core::Error *error) const override;
    QString stateToken() const override;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::lore
