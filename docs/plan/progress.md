# Implementation Progress

Status of the increments defined in
[pimio-v1-implementation.md](pimio-v1-implementation.md). An increment is
**Complete** only when every deliverable exists and its listed automated
acceptance evidence runs in CI. Anything else is **In progress** or **Not
started**.

Update this file in the same change that moves an increment forward.

| # | Increment | Status |
| --- | --- | --- |
| 0 | Project skeleton and observable CI | Complete |
| 1 | Stable contracts and test corpus | Complete |
| 2 | LORE feasibility gate | Complete (go) |
| 3 | SQLite projection, migrations, and jobs | Complete |
| 4 | Incremental scan and media identity | Complete |
| 5 | Metadata read, query, and search | Not started |
| 6 | Thumbnails, models, and basic browser | Not started |
| 7 | Watching and reconciliation | Not started |
| 8 | Save, portable metadata, and image recipes | Not started |
| 9 | Timestamp repair and organization workflows | Not started |
| 10 | Video playback, trim, and scene suggestions | Not started |
| 11 | Basic location | Not started |
| 12 | Resilience, performance, packaging, release candidate | Not started |

## Increment 0 — Project skeleton and observable CI — Complete

- CMake and CMake Presets project on a pinned Qt 6 release, `pimio::core`
  library, `pimio_app` shell, and Qt Test executables.
- Repository conventions in [../conventions.md](../conventions.md); stubs for
  [../supported-platforms.md](../supported-platforms.md) and
  [../dependency-bom.md](../dependency-bom.md).
- Evidence: `core.version` and `app.smoke` run through CTest on Linux, Windows,
  and macOS.

## Increment 1 — Stable contracts and test corpus — Complete

- UI-independent identity, metadata, edit-recipe, job, and error types, plus the
  storage, metadata, media-request, clock, and filesystem boundaries.
- Committed fixtures under `tests/fixtures/data/` with a provenance manifest.
- Evidence: `core.serialization`, `core.contracts`, `fixtures.manifest`.

## Increment 2 — LORE feasibility gate — Complete (go)

- LORE adapter behind `pimio::lore`, acquired through a pinned,
  checksum-verified `cmake/PimioLore.cmake` download.
- Decision recorded in
  [../decisions/0001-lore-durable-store.md](../decisions/0001-lore-durable-store.md):
  **go**, with a single-writer lock and an interrupted-write repair.
- Evidence: `lore.adapter`, `lore.faults`, `lore.projection`.

## Increment 3 — SQLite Projection, Migrations, and Jobs — Complete

- Versioned SQLite schema, migration runner, WAL configuration, transactions,
  and a projection rebuildable from the durable store.
- Persistent priority job queue (`pimio::projection::JobQueue`) backed by a
  separate SQLite database, with coalescing, retries, cancellation, and
  interrupted-run recovery.
- `JobDispatcher` driving bounded-concurrency execution via a private thread
  pool, with all SQLite mutations posted back to the owning thread.
- Evidence: `projection.migrations`, `projection.rebuild`, `projection.jobs`
  (covers jobs survive restart, run once logically, respect priority, recover
  from cancellation, and recover from injected failures).

## Increment 4 — Incremental scan and media identity — Complete

- `pimio::scan::Scanner` backed by `core::FileSystem` and `core::DurableStore`
  abstractions; no real disk or metadata library required.
- `MediaHasher` computes SHA-256 content fingerprints.
- Incremental reconciliation: new files get fresh `MediaId`s, unchanged files
  are skipped cheaply via size/mtime, moved/renamed files retain their existing
  `MediaId`, deleted files are removed from the store, duplicates receive
  independent ids.
- Symbolic-link policy (skip by default, optional follow), per-file permission
  and disappearing-file handling as non-fatal warnings, cancellation with
  staged-change discard.
- `DurableStore::remove()` added to the core interface; implemented in
  `MemoryDurableStore` (via staged removals) and `LoreDurableStore` (via
  staging-area tombstones).
- Evidence: `scan.incremental` — covers add, unchanged, update, delete, rename,
  move, duplicate, symlink policy, permission failure, disappearing file,
  restart idempotency, unavailable root, cancellation, metadata reader
  integration, and large-tree benchmark (1000 files).
