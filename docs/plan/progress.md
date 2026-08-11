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
| 5 | Metadata read, query, and search | Complete |
| 6 | Thumbnails, models, and basic browser | Complete |
| 7 | Watching and reconciliation | Complete |
| 7.5 | Browsing controls and settings | Complete |
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

## Increment 5 — Metadata read, query, and search — Complete

- `pimio::metadata::BuiltinMetadataReader` implements `core::MetadataReader`
  with parsers written for this project: JPEG/TIFF EXIF, PNG headers, XMP
  packets, and ISO base media boxes. No metadata library is linked; the choice
  and what would reverse it are recorded in
  [../decisions/0002-metadata-adapter.md](../decisions/0002-metadata-adapter.md).
- Precedence is explicit — UserEdit > Sidecar > Embedded > FileSystem — and
  every disagreement between a sidecar and the embedded block is kept as a
  `core::MetadataConflict` carrying both values and both origins.
- A capture time with no zone information stays offset-unknown instead of being
  assumed to be UTC; a file with no capture time at all falls back to the
  filesystem under `MetadataOrigin::FileSystem`, so the fallback is visible.
- Failure policy: an unrecognized container is `UnsupportedMedia` and a
  recognized but broken one is `CorruptData`, both recorded by the scan as
  warnings so a bad file never blocks it. Recoverable damage, such as a corrupt
  EXIF block in a readable JPEG, is a warning and the item is still indexed.
- Chronological queries, filters, pagination, and FTS5 full-text search live in
  `pimio::projection::ProjectionDatabase`; ordering is total (`capture_sort_key`
  then `id`) so equal, missing, and zone-less timestamps still page
  deterministically.
- Evidence: `metadata.golden` (capture-time precedence, timezone
  presence/absence, rotation, GPS, camera fields, video duration, audio-track
  presence, malformed values, unsupported media, scan integration) and
  `projection.metadata` (ordering, pagination, filters, Unicode and
  operator-character search, conflicts, and missing timestamps).

## Increment 6 — Thumbnails, Models, and Basic Browser

**Status: Complete**

### Deliverables

- `pimio::thumbnail::ThumbnailDiskCache` — persistent fingerprint-keyed JPEG
  cache with LRU trim and atomic writes (`QSaveFile`).
- `pimio::thumbnail::ThumbnailRenderer` — abstract renderer interface.
- `pimio::thumbnail::ImageRenderer` — Qt `QImageReader`-based renderer with
  EXIF rotation, configurable target size, and JPEG output.
- `pimio::thumbnail::VideoFrameRenderer` and `CompositeRenderer` — Qt
  Multimedia video-frame decoding behind the same renderer interface.
- `pimio::thumbnail::ThumbnailService` — `core::MediaRequestService`
  implementation backed by `QThreadPool`; per-request cancel flags;
  callbacks delivered via `QMetaObject::invokeMethod(Qt::QueuedConnection)`.
- `pimio::browser::MediaLibraryModel` — `QAbstractListModel` backed by
  `ProjectionDatabase`; lazy thumbnail loading; `setVisibleRange` with
  configurable prefetch margin; `ThumbnailStatus` role per row.
- Basic GridView QML UI in `src/app/qml/Main.qml`: toolbar, tile delegates
  with placeholder/thumbnail/video-badge states, empty-library splash.
- `pimio::app::LibrarySession` — `--library` startup wiring for the durable
  store, projection, scanner, thumbnail service, browser model, and watchers.
- `pimio::browser::ThumbnailImageProvider` — serves completed model thumbnails
  to QML through `image://thumbnail/<mediaId>`.
- Selectable progressive detail view: the grid first has a thumbnail source
  available, then asynchronously loads the original image for full-size
  display; Escape and the Close button return to the grid.
- `docs/plan/manual-testing.md` — manual test plan for cases that require a
  real display server or hardware.

### Automated evidence

- `thumbnail.cache` (11 subtests) — round-trip, corrupt-entry detection,
  fingerprint invalidation, LRU trim by mtime, `totalSize`.
- `thumbnail.service` (7 subtests) — cache hit/miss, delivery, completed-request
  cleanup, cancellation, `cancelAllExcept`, error delivery, priority ordering.
- `thumbnail.video` (7 subtests) — real-frame decoding, requested position,
  unsupported/error behavior, and image/video composite dispatch.
- `browser.model` (18 subtests including `QAbstractItemModelTester`) — row
  count, roles (MediaId, absolutePath, captureTimeString, MediaKind,
  ThumbnailStatus), detail-view lookup, visible-range request/cancel lifecycle,
  result/error callbacks, reload.
- `browser.thumbnail_image_provider` (5 subtests) — lookup, scaling, clearing,
  unknown IDs, and normalized IDs.
- `app.smoke` — creates the grid and detail view, scrolls a 100-row synthetic
  model, verifies the visible range changes, and opens a selected item.

### Manual testing

See `docs/plan/manual-testing.md` for Increment 6 manual coverage. The shipped
app accepts repeatable `--library <path>` options, so the thumbnail and browser
checks are now runnable against real library roots.

## Increment 7 — Watching and Reconciliation

**Status: Complete**

### Deliverables

- `pimio::watch::EventCoalescer` — deterministic debounce, rename pairing,
  overflow tracking, and periodic missed-event fallback.
- `pimio::watch::QtDirectoryWatchAdapter` — recursive portable watcher with
  normalized create, modify, remove, and overflow events.
- `pimio::watch::WatchService` — converts coalesced events and periodic
  fallbacks into durable, low-priority `ReconcileRoot` jobs.
- `pimio::watch::runReconcileJob` — full-scan reconciliation worker shared by
  startup scans and watch jobs.

### Automated evidence

- `watch.contract` (13 subtests) — create, burst, duplicate, reordered rename,
  unpaired rename, overflow, dropped-event fallback, and debounce behavior.
- `watch.native` (7 subtests) — native create, modify, remove, rename, recursive
  subdirectory, start failure, and stop behavior on each platform.
- `watch.reconciliation` (5 subtests) — incremental and dropped-event paths
  converge to the same durable store and projection as a clean scan, and
  startup overflow is preserved.

## Increment 7.5 — Browsing Controls and Settings — Complete

**Status: Complete**

### Deliverables

- `pimio::settings::Settings` — every user-visible setting in one place, split
  into stored settings persisted to an application-wide `pimio.conf` and
  session settings that reset at every launch, with clamping and tolerant
  parsing. Rationale in
  [../decisions/0003-settings-and-view-controls.md](../decisions/0003-settings-and-view-controls.md).
- `ProjectionDatabase::idsSorted()` and migration 3 (`file_extension` column and
  sort indexes) — sorting by capture time, file name, file date, file type, and
  file size in either direction.
- `MediaLibraryModel` sorting and tile-size-driven thumbnail tiers (128/256/512),
  so a larger tile is served a larger thumbnail rather than an upscaled one.
- Browser controls: sort selector and direction, live tile-size slider, keyboard
  navigation with hold-to-accelerate in both the grid and the preview,
  configurable wheel scrolling with optional in-gesture acceleration, a settings
  dialog, and a session-only tile diagnostics overlay.

### Automated evidence

- `settings.store` (16 subtests) — defaults, clamping, persistence across
  instances, corrupt-file tolerance, reset, and that session settings are never
  written.
- `projection.sort` (9 subtests) — every sort key in both directions, ties,
  missing file dates, and files without an extension.
- `browser.model` — re-sorting, unknown sort keys, tier selection from tile
  size, and re-requesting the visible window after a size change.
- `app.smoke` — arrow and page keys, key-hold acceleration and its off switch,
  wheel scrolling at two speeds and its bounds, the tile-size binding, preview
  stepping, and the settings dialog.
