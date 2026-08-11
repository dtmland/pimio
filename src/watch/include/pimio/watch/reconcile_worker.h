#pragma once

#include "pimio/core/durable_store.h"
#include "pimio/core/error.h"
#include "pimio/core/job.h"
#include "pimio/projection/projection_database.h"
#include "pimio/scan/library_root.h"
#include "pimio/scan/scanner.h"

#include <QJsonObject>

#include <atomic>

namespace pimio::watch {

/// Builds the JobRecord payload carrying \a root, shared by both ScanRoot
/// (initial, manual) and ReconcileRoot (watch-triggered) jobs so one worker
/// function can serve both kinds.
QJsonObject makeRootJobPayload(const scan::LibraryRoot &root);

/// Inverse of makeRootJobPayload(). Returns a default-constructed
/// LibraryRoot (empty absolutePath) when \a payload does not describe one.
scan::LibraryRoot rootFromJobPayload(const QJsonObject &payload);

/// Runs \a scanner over the root described by \a job's payload and, on
/// success, rebuilds \a projection from \a store.
///
/// This is the single place a ScanRoot or ReconcileRoot job actually does
/// work: JobDispatcher workers are thin lambdas that bind their scanner,
/// projection, and store and forward to this function, and tests exercise
/// this function directly without needing a JobQueue or JobDispatcher at
/// all. \a outResult, when non-null, receives the per-file outcome counts
/// so a caller (or a test proving reconciliation converges to a clean scan)
/// can inspect what changed.
///
/// Returns a non-empty Error only for a hard failure (bad payload, scan
/// root unavailable, or a failed projection rebuild); per-file problems are
/// accumulated as warnings in \a outResult exactly as Scanner::scan()
/// documents.
///
/// \a onProgress is handed straight to Scanner::scan(), so a caller that
/// configured the scanner to commit in batches is told about each one while
/// the scan is still running.
core::Error runReconcileJob(const core::JobRecord &job, const std::atomic<bool> &isCancelled,
                           scan::Scanner &scanner, projection::ProjectionDatabase *projection,
                           const core::DurableStore &store,
                           scan::Scanner::Result *outResult = nullptr,
                           const scan::Scanner::ProgressCallback &onProgress = {});

} // namespace pimio::watch
