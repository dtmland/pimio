#pragma once

#include "pimio/core/clock.h"
#include "pimio/core/durable_store.h"

#include <QHash>
#include <QSet>

namespace pimio::testing {

/// In-memory DurableStore used to exercise the storage contract without LORE.
///
/// It models the parts of the contract that matter for correctness: staging is
/// separate from history, a failed commit keeps staged changes, and the state
/// token changes only when a commit succeeds.
class MemoryDurableStore final : public core::DurableStore
{
public:
    explicit MemoryDurableStore(core::Clock &clock);

    /// Makes the next commit fail with \a code. The staged changes must
    /// survive.
    void failNextCommit(core::ErrorCode code);

    /// Makes the store report itself unavailable, as a missing or locked
    /// repository would.
    void setAvailable(bool available);

    /// Simulates a change made outside pimio, for example through a CLI.
    void applyExternalChange(const core::MediaRecord &record);

    // DurableStore
    bool isAvailable() const override;
    bool createLibrary(const QString &name, core::Error *error) override;
    std::optional<core::LibraryDescriptor> libraryDescriptor(core::Error *error) const override;
    bool stage(const core::MediaRecord &record, core::Error *error) override;
    bool stageOriginal(const core::MediaRecord &record, const QString &sourcePath,
                       core::Error *error) override;
    QString originalPath(const core::MediaRecord &record,
                         core::Error *error) const override;
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
    void bumpStateToken();

    core::Clock &m_clock;
    bool m_available = true;
    std::optional<core::ErrorCode> m_nextCommitFailure;
    std::optional<core::LibraryDescriptor> m_library;

    QHash<QString, core::MediaRecord> m_committed;
    QHash<QString, core::MediaRecord> m_staged;
    QSet<QString> m_stagedRemovals;
    QSet<QString> m_committedOriginals;
    QSet<QString> m_stagedOriginals;
    QList<core::Checkpoint> m_history;
    quint64 m_stateCounter = 0;
};

} // namespace pimio::testing
