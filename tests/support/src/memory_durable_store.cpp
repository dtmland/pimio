#include "pimio/testing/memory_durable_store.h"

#include <algorithm>

namespace pimio::testing {
namespace {

void setError(core::Error *target, core::ErrorCode code, const QString &message)
{
    if (target) {
        *target = core::Error(code, message);
    }
}

} // namespace

MemoryDurableStore::MemoryDurableStore(core::Clock &clock)
    : m_clock(clock)
{
}

void MemoryDurableStore::failNextCommit(core::ErrorCode code)
{
    m_nextCommitFailure = code;
}

void MemoryDurableStore::setAvailable(bool available)
{
    m_available = available;
}

void MemoryDurableStore::applyExternalChange(const core::MediaRecord &record)
{
    m_committed.insert(record.id.value(), record);

    core::Checkpoint checkpoint;
    checkpoint.id = QStringLiteral("external-%1").arg(m_history.size() + 1);
    checkpoint.message = QStringLiteral("External change");
    checkpoint.createdAtUtc = m_clock.nowUtc();
    m_history.prepend(checkpoint);

    bumpStateToken();
}

bool MemoryDurableStore::isAvailable() const
{
    return m_available;
}

bool MemoryDurableStore::stage(const core::MediaRecord &record, core::Error *error)
{
    if (!m_available) {
        setError(error, core::ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    if (!record.id.isValid()) {
        setError(error, core::ErrorCode::Internal,
                 QStringLiteral("A record must have a valid media id."));
        return false;
    }
    m_staged.insert(record.id.value(), record);
    return true;
}

std::optional<core::Checkpoint> MemoryDurableStore::commit(const QString &message,
                                                           core::Error *error)
{
    if (!m_available) {
        setError(error, core::ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return std::nullopt;
    }
    if (m_nextCommitFailure) {
        const core::ErrorCode code = *m_nextCommitFailure;
        m_nextCommitFailure.reset();
        // Staged changes are deliberately left untouched: a failed commit must
        // be recoverable, not destructive.
        setError(error, code, QStringLiteral("The commit failed."));
        return std::nullopt;
    }
    if (m_staged.isEmpty() && m_stagedRemovals.isEmpty()) {
        setError(error, core::ErrorCode::Conflict,
                 QStringLiteral("There are no staged changes to commit."));
        return std::nullopt;
    }

    for (auto it = m_staged.constBegin(); it != m_staged.constEnd(); ++it) {
        m_committed.insert(it.key(), it.value());
    }
    m_staged.clear();

    for (const QString &id : std::as_const(m_stagedRemovals)) {
        m_committed.remove(id);
    }
    m_stagedRemovals.clear();

    core::Checkpoint checkpoint;
    checkpoint.id = QStringLiteral("checkpoint-%1").arg(m_history.size() + 1);
    checkpoint.message = message;
    checkpoint.createdAtUtc = m_clock.nowUtc();
    m_history.prepend(checkpoint);

    bumpStateToken();
    return checkpoint;
}

bool MemoryDurableStore::discardStaged(core::Error *)
{
    m_staged.clear();
    m_stagedRemovals.clear();
    return true;
}

bool MemoryDurableStore::hasStagedChanges() const
{
    return !m_staged.isEmpty() || !m_stagedRemovals.isEmpty();
}

bool MemoryDurableStore::remove(const core::MediaId &id, core::Error *error)
{
    if (!m_available) {
        setError(error, core::ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return false;
    }
    // Removing a staged update supersedes it.
    m_staged.remove(id.value());
    m_stagedRemovals.insert(id.value());
    return true;
}

std::optional<core::MediaRecord> MemoryDurableStore::load(const core::MediaId &id,
                                                          core::Error *error) const
{
    if (!m_available) {
        setError(error, core::ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return std::nullopt;
    }
    const auto it = m_committed.constFind(id.value());
    if (it == m_committed.constEnd()) {
        setError(error, core::ErrorCode::NotFound, QStringLiteral("No such record."));
        return std::nullopt;
    }
    return it.value();
}

QList<core::MediaId> MemoryDurableStore::listIds(core::Error *error) const
{
    if (!m_available) {
        setError(error, core::ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return {};
    }
    QStringList values = m_committed.keys();
    std::sort(values.begin(), values.end());

    QList<core::MediaId> ids;
    ids.reserve(values.size());
    for (const QString &value : std::as_const(values)) {
        ids.append(core::MediaId(value));
    }
    return ids;
}

QList<core::Checkpoint> MemoryDurableStore::history(int limit, core::Error *error) const
{
    if (!m_available) {
        setError(error, core::ErrorCode::StorageUnavailable,
                 QStringLiteral("The durable store is unavailable."));
        return {};
    }
    if (limit < 0 || limit >= m_history.size()) {
        return m_history;
    }
    return m_history.first(limit);
}

QString MemoryDurableStore::stateToken() const
{
    return QStringLiteral("memory-%1").arg(m_stateCounter);
}

void MemoryDurableStore::bumpStateToken()
{
    ++m_stateCounter;
}

} // namespace pimio::testing
