#pragma once

#include "pimio/core/durable_store.h"
#include "pimio/core/error.h"
#include "pimio/core/file_system.h"
#include "pimio/core/metadata_reader.h"
#include "pimio/scan/library_root.h"

#include <QList>

#include <atomic>
#include <functional>

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

    /// Reports records that have just been committed, and the counts so far.
    ///
    /// Called on the thread running the scan, with the scan paused, so a
    /// handler may hand the records to another thread but must not block for
    /// long: the scan resumes when it returns.
    using ProgressCallback =
            std::function<void(const QList<core::MediaRecord> &committed, const Result &progress)>;

    /// Constructs a scanner.
    ///
    /// \a reader may be null; when null, all records are stored with
    /// MediaKind::Unknown. Providing a reader gives richer metadata but does
    /// not affect identity or fingerprint correctness.
    Scanner(core::FileSystem *fs, core::MetadataReader *reader, core::DurableStore *store);

    ~Scanner();

    Scanner(const Scanner &) = delete;
    Scanner &operator=(const Scanner &) = delete;

    /// Number of records staged before the scan commits them and reports them
    /// through its progress callback. 0, the default, commits once at the end
    /// of the scan.
    ///
    /// Batching is what lets a browser show a library while it is still being
    /// indexed: nothing outside the store can see a record until it is
    /// committed, so a single commit at the end means one long wait followed
    /// by every item appearing at once. It is a trade: each commit costs a
    /// durable write, so a batch that is too small spends the scan committing.
    /// Values below 0 are treated as 0.
    void setCommitBatchSize(int records);
    int commitBatchSize() const;

    /// Scans \a root and synchronises the durable store.
    ///
    /// Returns an empty Error on success. Returns a non-empty Error for hard
    /// failures (root does not exist, store unavailable). Per-file errors are
    /// accumulated in \a *result, not returned as hard failures.
    ///
    /// \a onProgress, when set, is called after every batch commit (see
    /// setCommitBatchSize()) with the records that commit made durable.
    ///
    /// Sets the cancellation flag via \a isCancelled. When cancellation is
    /// requested, any staged changes are discarded. With batching switched on,
    /// batches already committed stay committed: they describe files that
    /// really are on disk, and the scan is idempotent, so the next run
    /// converges on the same result rather than redoing work.
    core::Error scan(const LibraryRoot &root, const std::atomic<bool> &isCancelled,
                     Result *result = nullptr, const ProgressCallback &onProgress = {});

private:
    class Private;
    Private *d;
};

} // namespace pimio::scan
