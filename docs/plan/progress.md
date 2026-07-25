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
| 3 | SQLite projection, migrations, and jobs | In progress |
| 4 | Incremental scan and media identity | Not started |
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

## Increment 3 — SQLite projection, migrations, and jobs — In progress

Done:

- Versioned SQLite schema, migration runner, WAL configuration, transactions,
  and a projection rebuildable from the durable store.
- Evidence: `projection.migrations` (empty, current, previous-version,
  interrupted-migration, and corrupt-cache cases) and `projection.rebuild`.

Outstanding:

- Persistent priority job queue with retries, cancellation, progress reporting,
  and bounded concurrency. `pimio::core::JobRecord` defines the contract only;
  no queue implementation or worker registry exists yet.
- Evidence still required: jobs survive restart, run once logically, respect
  priority, and recover from cancellation and injected failures.

## Increment 4 — Incremental scan and media identity — Not started

Blocked on the Increment 3 job queue, which the scan runs on.

Deliverables: multiple library roots, incremental traversal, stable identity,
content fingerprints, move/rename recognition, unavailable-root reporting, and
no UI beyond diagnostic query output.

Evidence required: temporary-directory tests for add, update, rename, move,
delete, duplicate, symlink policy, permission failure, disappearing files, and
restart; a repeated unchanged scan performing no logical updates; and a recorded
benchmark on a generated large tree within the agreed memory and concurrency
bounds.
