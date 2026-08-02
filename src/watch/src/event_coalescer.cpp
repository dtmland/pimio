#include "pimio/watch/event_coalescer.h"

namespace pimio::watch {

EventCoalescer::EventCoalescer(int debounceMs, int renamePairingTimeoutMs)
    : m_debounceMs(debounceMs)
    , m_renamePairingTimeoutMs(renamePairingTimeoutMs)
{
}

void EventCoalescer::markPending(const QDateTime &now)
{
    if (!m_pending) {
        m_pending = true;
        m_pendingSince = now;
    }
    ++m_pendingEventCount;
}

void EventCoalescer::ingest(const WatchEvent &event, const QDateTime &now)
{
    if (event.kind == WatchEventKind::Overflow) {
        m_overflowPending = true;
        // An overflow may have swallowed one half of an in-flight rename; a
        // pairing built from partial information could pair the wrong
        // events together, so drop it rather than trust it.
        m_pendingRenameHalves.clear();
        markPending(now);
        return;
    }

    if ((event.kind == WatchEventKind::RenamedFrom || event.kind == WatchEventKind::RenamedTo)
        && event.renameCookie != 0) {
        const auto it = m_pendingRenameHalves.find(event.renameCookie);
        if (it != m_pendingRenameHalves.end()) {
            m_pendingRenameHalves.erase(it);
            ++m_pairedRenameCount;
        } else {
            m_pendingRenameHalves.insert(event.renameCookie, PendingHalf{event, now});
        }
        markPending(now);
        return;
    }

    markPending(now);
}

bool EventCoalescer::hasPendingChange(const QDateTime &now) const
{
    Q_UNUSED(now);
    return m_pending;
}

bool EventCoalescer::takeDueReconcile(const QDateTime &now)
{
    for (auto it = m_pendingRenameHalves.begin(); it != m_pendingRenameHalves.end();) {
        if (it.value().receivedAt.msecsTo(now) >= m_renamePairingTimeoutMs) {
            it = m_pendingRenameHalves.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_pending) {
        return false;
    }
    if (m_pendingSince.msecsTo(now) < m_debounceMs) {
        return false;
    }

    m_lastReconcileEventCount = m_pendingEventCount;
    m_lastReconcileWasOverflow = m_overflowPending;

    m_pending = false;
    m_pendingEventCount = 0;
    m_overflowPending = false;
    return true;
}

bool EventCoalescer::lastReconcileWasOverflow() const
{
    return m_lastReconcileWasOverflow;
}

int EventCoalescer::lastReconcileEventCount() const
{
    return m_lastReconcileEventCount;
}

int EventCoalescer::pairedRenameCount() const
{
    return m_pairedRenameCount;
}

int EventCoalescer::pendingRenameHalfCount() const
{
    return m_pendingRenameHalves.size();
}

bool EventCoalescer::isPeriodicFallbackDue(const QDateTime &lastReconcileAt, const QDateTime &now,
                                           qint64 intervalMs)
{
    if (!lastReconcileAt.isValid()) {
        return true;
    }
    return lastReconcileAt.msecsTo(now) >= intervalMs;
}

} // namespace pimio::watch
