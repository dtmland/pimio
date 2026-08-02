#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace pimio::watch {

/// Normalized kind of a raw filesystem-change notification.
///
/// This is the portable contract every WatchAdapter reports through,
/// independent of what any particular native API can distinguish. An
/// adapter that cannot tell a create from a modification is free to report
/// Modified for both: EventCoalescer treats any of Created, Modified,
/// Removed, RenamedFrom, or RenamedTo as "this root needs reconciling",
/// so under-reporting precision is always safe, only less informative.
enum class WatchEventKind {
    Created,
    Modified,
    Removed,

    /// The old half of a rename or move. Pairs with a RenamedTo that shares
    /// the same non-zero renameCookie.
    RenamedFrom,

    /// The new half of a rename or move.
    RenamedTo,

    /// The adapter's own notification channel overflowed (for example,
    /// inotify's IN_Q_OVERFLOW, or ReadDirectoryChangesW's buffer overflow
    /// on Windows): some events under \c path were lost and cannot be
    /// recovered from the channel itself. \c path is the deepest directory
    /// the adapter still vouches for, often the watched root itself.
    Overflow,
};

QString toString(WatchEventKind kind);

/// One raw, normalized filesystem-change notification.
///
/// WatchAdapter implementations produce these; EventCoalescer consumes them.
/// Neither side needs to know anything about the other kind's OS-specific
/// origin, which is what makes both independently testable.
struct WatchEvent
{
    WatchEventKind kind = WatchEventKind::Modified;

    /// Absolute path the event concerns. For Overflow, the subtree that may
    /// have missed events.
    QString path;

    /// Correlates a RenamedFrom with its RenamedTo half. Two halves sharing
    /// the same non-zero cookie describe one rename. Native rename-cookie
    /// schemes may deliver the two halves out of order under load, so
    /// pairing must not assume RenamedFrom arrives before RenamedTo.
    ///
    /// Zero means "no cookie": the event is treated as a standalone change
    /// rather than half of a pair.
    quint32 renameCookie = 0;

    /// When the adapter observed the change. Used only to order and age out
    /// events for coalescing; it is never persisted.
    QDateTime observedAt;

    bool operator==(const WatchEvent &other) const = default;
};

} // namespace pimio::watch

Q_DECLARE_METATYPE(pimio::watch::WatchEvent)
