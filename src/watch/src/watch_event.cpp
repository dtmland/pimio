#include "pimio/watch/watch_event.h"

namespace pimio::watch {

QString toString(WatchEventKind kind)
{
    switch (kind) {
    case WatchEventKind::Created:
        return QStringLiteral("created");
    case WatchEventKind::Modified:
        return QStringLiteral("modified");
    case WatchEventKind::Removed:
        return QStringLiteral("removed");
    case WatchEventKind::RenamedFrom:
        return QStringLiteral("renamedFrom");
    case WatchEventKind::RenamedTo:
        return QStringLiteral("renamedTo");
    case WatchEventKind::Overflow:
        return QStringLiteral("overflow");
    }
    return QStringLiteral("unknown");
}

} // namespace pimio::watch
