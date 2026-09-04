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
| 7.6 | Progressive scanning and thumbnail retention | Complete |
| 7.7 | Library, revision, and author identity | Complete |
| 7.8 | Library storage-model gate (managed originals) | Complete (historical decision reopened) |
| 7.8a | LORE 0.9 adoption and recovery simplification | Not started |
| 7.8b | Offline-to-server promotion gate | Not started |
| 7.8c | Storage-model decision revisit | Not started |
| 7.9 | Library manager and lifecycle | Not started |
| 8 | Save, portable metadata, and image recipes | Not started |
| 9 | Timestamp repair and organization workflows | Not started |
| 10 | Video playback, trim, and scene suggestions | Not started |
| 11 | Basic location | Not started |
| 12 | Resilience, performance, packaging, release candidate | Not started |

Increments 7.7–7.9, including the 7.8a–7.8c correction gates, were added when
the plans were reoriented around the
library-centric LORE design; the gap analysis motivating them is recorded in
[pimio-v1-implementation.md](pimio-v1-implementation.md#reorientation-the-library-centric-direction).

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

## Increment 7.6 — Progressive scanning and thumbnail retention — Complete

Reported symptoms: a window that looks frozen while a library is first scanned,
tiles that all appear at once at the end, and thumbnails that decay into grey
tiles the longer the application is scrolled. Rationale in
[../decisions/0004-progressive-scan-and-thumbnail-retention.md](../decisions/0004-progressive-scan-and-thumbnail-retention.md).

### Deliverables

- `scan::Scanner::setCommitBatchSize()` and a progress callback — the scan
  commits in batches and reports each committed batch, so the grid fills as the
  scan walks the tree instead of at the end.
- `ProjectionDatabase::applyRecords()` — projects one committed batch in a
  single transaction, deliberately without advancing the projected state token
  so `isStale()` stays truthful.
- `MediaLibraryModel` thumbnail retention — a bounded most-recently-used list;
  an evicted id is removed from the image provider and its row returns to
  `Pending`, which is the fix for the grey tiles. `reload()` now preserves
  loaded thumbnails and inserts new rows at their sorted positions without
  resetting the model.
- `MediaLibraryModel::refreshThumbnail()` and a one-shot QML retry when an
  `Image` reports an error.
- `app::LibraryActivity` — a busy indicator, a "Scanning… N found" label, and a
  placeholder for an empty grid during the first scan.
- `scanBatchSize` stored setting (8–2048, default 64) with a settings-dialog
  slider.

### Automated evidence

- `browser.model` — the retention bound drops old thumbnails rather than
  claiming rows the provider cannot serve, a row scrolled back into view is
  requested again, `refreshThumbnail()` re-requests, and insertion-only
  `reload()` calls keep loaded thumbnails without a model reset.
- `browser.thumbnailImageProvider` — capacity, removal, and containment.
- `scan.incremental` — a batched scan is readable from the store before it
  finishes and reports cumulative counts, an unbatched scan still commits once,
  and a cancelled batched scan keeps what it committed.
- `projection.rebuild` — `applyRecords()` adds and replaces rows and leaves the
  state token alone.
- `settings.store` — `scanBatchSize` default, clamping, signals, persistence,
  and reset.
- `app.smoke` — a thumbnail the provider cannot serve is asked for again, scan
  startup shows feedback before storage opens, and resized grids use their
  shifted layout origin for scroll bounds and thumbnail requests.

## Increment 7.7 — Library, revision, and author identity — Complete

### Deliverables

- `core::LibraryDescriptor` is stored at a reserved path in each LORE
  repository and carries a generated library id, display name, format version,
  creation timestamp, and stable implicit-local-user id.
- `core::Checkpoint` carries author id, application version, and parent id.
  pimio encodes that provenance in LORE checkpoint metadata while reading
  pre-extension history with an `unknown` author and empty unavailable fields.
- The read/write/administer/share service authorization boundary and v1's
  grant-all policy for the implicit user are documented in
  [../library-model.md](../library-model.md).
- Projection, job, and thumbnail cache paths are keyed by the descriptor's
  library id. The current path-derived value is retained only to locate the
  repository until the Increment 7.9 Library Manager replaces that entry point.

### Automated evidence

- `core.serialization` — library and checkpoint round trips, preservation of
  unknown fields, schema markers, and defaults for pre-extension checkpoints.
- `core.contracts` — independently created identities differ, the implicit user
  receives every v1 permission, and checkpoints link to their parent with
  author and application provenance.
- `lore.adapter` — a moved repository retains its library id, an independent
  repository gets another id, and provenance survives history reload.

## Increment 7.8 — Library storage-model gate — Complete (decision reopened)

### Deliverables

- A reproducible `lore.binary_content` spike commits, restarts, restores, and
  SHA-256-verifies deterministic binary content through the pinned LORE CLI.
  It measures commit/read-back cost, repository size, and identical-content
  deduplication; `PIMIO_LORE_BINARY_SPIKE_MIB=256` selects the recorded
  multi-hundred-MB run.
- [Decision 0005](../decisions/0005-managed-versus-referenced-originals.md)
  recorded an initial no-go for managed originals. LORE passed binary integrity
  and deduplication, but the checkout and immutable copy roughly doubled
  original storage while pimio's pre-commit recovery backup scaled with the
  complete durable corpus.
- [Decision 0006](../decisions/0006-local-first-lore-topology.md) removes that
  whole-store backup from the target architecture, so the storage conclusion is
  reopened. Current code still references originals; complete backups must
  include the configured media roots until Increment 7.8c decides otherwise.

### Automated evidence

- `lore.binary_content` — commits binary content, reloads it in a fresh LORE
  process, verifies its SHA-256, and demonstrates content deduplication across
  two paths.

## Increments 7.8a–7.8c — Architecture correction — Not started

- **7.8a:** upgrade every build context to LORE 0.9.0, adapt and fully retest the
  private API boundary, verify 0.8.5 repository migration, and remove the
  `.pimio-lore-backup` transaction workaround without weakening visible failure
  handling or the acknowledged-checkpoint contract.
- **7.8b:** prove that an offline-origin Library can be registered, pushed to a
  test LORE server, and freshly cloned with identity, history, and bytes intact.
  This validates a future hosting path; it does not implement pimio Server in v1.
- **7.8c:** repeat storage economics through the production 0.9.0 path and make
  the final managed/referenced/both decision before Library Manager work.
- Release-note review found no explicit statement that 0.9.0 resolves pimio's
  three 0.8.5 interrupted-commit observations. Reproduce before filing the
  [prepared upstream issue drafts](lore-0.9-upstream-issue-drafts.md).
