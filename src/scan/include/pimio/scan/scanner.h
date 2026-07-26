#pragma once

#include "pimio/core/durable_store.h"
#include "pimio/core/error.h"
#include "pimio/core/file_system.h"
#include "pimio/core/metadata_reader.h"
#include "pimio/scan/library_root.h"

#include <QList>

#include <atomic>

namespace pimio::scan {

/// Incrementally scans a library root and synchronises discovered media
/// records with the durable store.
///
/// The scan is stateless between runs: all persistent state lives in the
/// durable store. Restarting after a crash is safe because the algorithm is
/// idempotent — records that were already committed are recognised as
/// unchanged and skipped.
///
/// Scan behaviour:
///  - New files receive a freshly generated MediaId.
///  - Moved or renamed files (same content fingerprint, absent from old path)
///    retain their existing MediaId.
///  - Duplicates (same fingerprint, multiple live paths) each get their own
///    MediaId.
///  - Deleted files (present in the store but absent from disk) are removed.
///  - Symbolic links are skipped unless LibraryRoot::followSymlinks is true.
///  - Per-file read errors are recorded as warnings; the scan continues.
class Scanner
{
public:
    /// Per-file outcome counts from a single scan run.
    struct Result
    {
        int added = 0;       ///< New records created.
        int updated = 0;     ///< Existing records whose identity or content changed.
        int removed = 0;     ///< Records absent from disk and removed from the store.
        int unchanged = 0;   ///< Records verified present and unmodified (cheaply).

        /// Non-fatal per-file errors. The file is skipped but the scan proceeds.
        QList<core::Error> warnings;
    };

    /// Constructs a scanner.
    ///
    /// \a reader may be null; when null, all records are stored with
    /// MediaKind::Unknown. Providing a reader gives richer metadata but does
    /// not affect identity or fingerprint correctness.
    Scanner(core::FileSystem *fs, core::MetadataReader *reader, core::DurableStore *store);

    ~Scanner();

    Scanner(const Scanner &) = delete;
    Scanner &operator=(const Scanner &) = delete;

    /// Scans \a root and synchronises the durable store.
    ///
    /// Returns an empty Error on success. Returns a non-empty Error for hard
    /// failures (root does not exist, store unavailable). Per-file errors are
    /// accumulated in \a *result, not returned as hard failures.
    ///
    /// Sets the cancellation flag via \a isCancelled. When cancellation is
    /// requested, any staged changes are discarded and the store is left in its
    /// pre-scan state.
    core::Error scan(const LibraryRoot &root, const std::atomic<bool> &isCancelled,
                     Result *result = nullptr);

private:
    class Private;
    Private *d;
};

} // namespace pimio::scan
