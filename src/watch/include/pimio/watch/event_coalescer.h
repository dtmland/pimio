#pragma once

#include "pimio/watch/watch_event.h"

#include <QDateTime>
#include <QHash>

namespace pimio::watch {

/// Coalesces a stream of raw WatchEvents into a series of "reconcile now"
/// decisions, without ever touching the filesystem, a job queue, or a real
/// timer itself.
///
/// Keeping this logic pure (fed events and a clock value, producing
/// decisions) is what makes the coalescing contract — duplicates, bursts,
/// reordered rename halves, overflow — deterministically testable: a test
/// drives \c ingest() and \c takeDueReconcile() with explicit QDateTime
/// values and never waits on a real timer.
///
/// Behaviour:
///  - Any Created, Modified, Removed, RenamedFrom, or RenamedTo event marks
///    the root pending. A burst of any number of such events before the
///    debounce window elapses still produces exactly one due reconcile.
///  - RenamedFrom/RenamedTo events that share a non-zero renameCookie are
///    paired as soon as both halves have been seen, regardless of the order
///    they arrive in. An unpaired half still marks the root pending (Scanner
///    does not need rename semantics to reconcile correctly); the pairing
///    only affects bookkeeping exposed for tests. A half whose partner never
///    arrives within the pairing timeout is dropped from the pairing table
///    without further effect.
///  - An Overflow event marks the root pending and clears any in-flight
///    rename pairing, since an overflow may have eaten one half of a pair;
///    the resulting reconcile is flagged so callers can log it distinctly.
///  - isPeriodicFallbackDue() is a separate, always-available safety net:
///    even a channel that drops an event without signalling overflow at all
///    is eventually corrected, because the fallback does not depend on
///    having observed anything.
class EventCoalescer
{
public:
    /// \a debounceMs is how long the coalescer waits after the first pending
    /// change before reporting the batch as due, so a burst collapses into
    /// one reconcile. \a renamePairingTimeoutMs is how long an unpaired
    /// rename half is kept in the pairing table before being dropped.
    explicit EventCoalescer(int debounceMs = 500, int renamePairingTimeoutMs = 2000);

    /// Feeds one raw event, observed at \a now, into the coalescer.
    void ingest(const WatchEvent &event, const QDateTime &now);

    /// True when a change is pending but has not yet waited out its debounce
    /// window at \a now.
    bool hasPendingChange(const QDateTime &now) const;

    /// Advances internal bookkeeping to \a now (aging out expired rename
    /// pairings) and, if a pending batch has waited out its debounce window,
    /// consumes it and returns true. Idempotent when nothing is due: calling
    /// it repeatedly with the same or later \a now returns false again until
    /// a new event arrives.
    bool takeDueReconcile(const QDateTime &now);

    /// True when the batch most recently returned by takeDueReconcile()
    /// included an overflow.
    bool lastReconcileWasOverflow() const;

    /// Number of raw events folded into the batch most recently returned by
    /// takeDueReconcile(). Exposed so tests can prove a burst or a run of
    /// duplicates collapses to one reconcile rather than one per event.
    int lastReconcileEventCount() const;

    /// Number of RenamedFrom/RenamedTo pairs successfully matched so far.
    /// Exposed so tests can prove a reordered pair is still recognised as
    /// one rename rather than two unrelated changes.
    int pairedRenameCount() const;

    /// Number of rename halves still waiting for their partner. Non-zero
    /// only within the pairing timeout window after a lone half arrives.
    int pendingRenameHalfCount() const;

    /// True when \a now is at least \a intervalMs past \a lastReconcileAt.
    /// A pure function so a periodic missed-event safety net can be tested
    /// without a real timer and without depending on any event ever having
    /// been observed.
    static bool isPeriodicFallbackDue(const QDateTime &lastReconcileAt, const QDateTime &now,
                                      qint64 intervalMs);

private:
    struct PendingHalf
    {
        WatchEvent event;
        QDateTime receivedAt;
    };

    void markPending(const QDateTime &now);

    int m_debounceMs;
    int m_renamePairingTimeoutMs;

    bool m_pending = false;
    QDateTime m_pendingSince;
    int m_pendingEventCount = 0;
    bool m_overflowPending = false;

    int m_lastReconcileEventCount = 0;
    bool m_lastReconcileWasOverflow = false;
    int m_pairedRenameCount = 0;

    QHash<quint32, PendingHalf> m_pendingRenameHalves;
};

} // namespace pimio::watch
