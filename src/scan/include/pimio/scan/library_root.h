#pragma once

#include <QString>

namespace pimio::scan {

/// A directory that pimio monitors and indexes.
///
/// Roots are independent: adding a second root that contains the same files as
/// an existing one results in duplicates, which the scan records but does not
/// resolve silently.
struct LibraryRoot
{
    QString absolutePath;

    /// When false (the default), symbolic links are not followed during
    /// traversal. Following symlinks risks cycles and out-of-root paths; callers
    /// that enable it take responsibility for those hazards.
    bool followSymlinks = false;
};

} // namespace pimio::scan
