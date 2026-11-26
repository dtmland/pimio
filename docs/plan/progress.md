# Implementation Progress

This is the single status board for the increments defined in
[pimio-v1-implementation.md](pimio-v1-implementation.md). An increment is
**Complete** only when every deliverable exists and its listed automated
acceptance evidence runs in CI. **In progress** means material deliverables or
evidence exist but the increment is not finished. Anything else is **Not
started**.

Update this file in the same change that moves an increment forward. Keep it
short: record the outcome and the evidence names, not a deliverable-by-
deliverable narrative. Older detail is available in git history.

## Status board

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
| 7.8 | Library storage-model gate (managed originals) | Complete (superseded by 7.8c) |
| 7.8a | LORE 0.9 adoption and recovery simplification | Complete |
| 7.8b | Offline-to-server promotion gate | Complete (promotion enabled; alpha risk accepted) |
| 7.8c | Storage-model decision revisit | Complete (managed originals for v1) |
| 7.9 | Library manager and lifecycle | Complete |
| 8 | Save, portable metadata, and image recipes | Complete |
| 9 | Timestamp repair and organization workflows | Not started |
| 10 | Video playback, trim, and scene suggestions | Not started |
| 11 | Basic location | Not started |
| 12 | Resilience, performance, packaging, release candidate | In progress (packaging partial) |

## Where the project stands

- **Done:** durable LORE-backed Library with stable identity, managed
  originals, scan/watch, metadata read and search, browser, Library Manager
  lifecycle (create/open/rename/move/backup/restore), offline-to-server
  promotion, Save with embedded metadata writes, and crop/rotation recipes.
  These are production code paths, not spikes.
- **Not started:** timestamp repair and organization (9), video playback and
  trim (10), and location assignment (11). GPS is read but cannot yet be
  edited; `EditOperationKind::Trim` is only reserved.
- **In progress:** release hardening (12). Unsigned multi-platform archives
  ship; the remaining release gates are listed below.
- **Active dependency work:** upgrade LORE 0.9.0 → 0.14.0, planned in
  [lore-0.14-upgrade.md](lore-0.14-upgrade.md). Known LORE defects and their
  workarounds are tracked in [lore-upstream-issues.md](lore-upstream-issues.md).
- **Known accepted alpha risk:** an interrupted first push during server
  promotion may need server-side repair
  ([EpicGames/lore#191](https://github.com/EpicGames/lore/issues/191)); the
  upgrade plan checks whether 0.14.0 removes it.
- **Supporting docs that match the code:** [library-model.md](../library-model.md),
  [library-service-api.md](../library-service-api.md), decisions 0001–0007,
  [testing.md](../testing.md), and [build-architecture.md](../build-architecture.md).

## Completed increments: outcome and evidence

CTest names are the acceptance evidence and run in CI on Linux, Windows, and
macOS, except display-bound `studio`-labeled tests such as
`studio.empty_library`, which run through the `studio` test preset. Offline build contracts run with
`python -m unittest discover -s tests/build -v` before provisioning in CI and
Release.

| # | Outcome | Evidence | Decision |
| --- | --- | --- | --- |
| 0 | CMake/presets project on pinned Qt 6, core library, app shell, Qt Test | `core.version`, `app.smoke` | — |
| 1 | UI-independent identity, metadata, recipe, job, and error types; storage, metadata, media, clock, and filesystem boundaries; owned fixtures | `core.serialization`, `core.contracts`, `fixtures.manifest` | — |
| 2 | LORE adapter behind `pimio::lore`, checksum-pinned download; go with single-writer lock and visible repair | `lore.adapter`, `lore.faults`, `lore.projection` | [0001](../decisions/0001-lore-durable-store.md) |
| 3 | Versioned SQLite projection rebuildable from the store; persistent priority job queue and dispatcher | `projection.migrations`, `projection.rebuild`, `projection.jobs` | — |
| 4 | Incremental scanner with SHA-256 identity that survives move/rename; symlink, permission, and cancellation policy | `scan.incremental`, `scan.qt_file_system` | — |
| 5 | Built-in EXIF/XMP/PNG/ISO-BMFF reader with explicit precedence and conflicts; total-order queries and FTS5 search | `metadata.golden`, `projection.metadata` | [0002](../decisions/0002-metadata-adapter.md) |
| 6 | Thumbnail cache, image/video renderers, browser model, QML grid and progressive detail view | `thumbnail.cache`, `thumbnail.service`, `thumbnail.video`, `browser.model`, `browser.thumbnail_image_provider`, `app.smoke`, `studio.empty_library` | — |
| 7 | Debounced native watching converging with full-scan reconciliation | `watch.contract`, `watch.native`, `watch.reconciliation` | — |
| 7.5 | Central settings, sorting, tile-size thumbnail tiers, keyboard/wheel navigation | `settings.store`, `projection.sort`, `browser.model`, `app.smoke` | [0003](../decisions/0003-settings-and-view-controls.md) |
| 7.6 | Batched progressive scan, bounded thumbnail retention, scan activity feedback | `scan.incremental`, `projection.rebuild`, `browser.model`, `app.smoke` | [0004](../decisions/0004-progressive-scan-and-thumbnail-retention.md) |
| 7.7 | Library descriptor id, checkpoint author/app-version/parent provenance, v1 grant-all authorization boundary | `core.serialization`, `core.contracts`, `lore.adapter` | [library-model.md](../library-model.md) |
| 7.8 | Binary-content spike; initial no-go for managed originals, later reversed by 7.8c | `lore.binary_content` | [0005](../decisions/0005-managed-versus-referenced-originals.md) |
| 7.8a | LORE 0.9.0 in every context; 0.8.5 store migration; whole-store backup workaround removed while keeping lock, flush-before-ack, staged input, checkout restore, visible repair | `lore.migration`, `lore.faults` | [0001](../decisions/0001-lore-durable-store.md), [0006](../decisions/0006-local-first-lore-topology.md) |
| 7.8b | Known-remote promote/push/clone preserves identity, records, bytes, and history (lazy history hydration); ID preflight; `config.toml` attach; interrupted-push retry accepted as alpha risk | `lore.server_promotion` | [0006](../decisions/0006-local-first-lore-topology.md) |
| 7.8c | Managed originals for v1; metadata commits corpus-independent; scanner stages originals with records | `lore.binary_content`, `scan.incremental` | [0005](../decisions/0005-managed-versus-referenced-originals.md) |
| 7.9 | `LibraryService`/`LibraryManager` lifecycle, checksummed `.pimio-backup`, restore rebuilds caches, Library Manager dialog | `app.library_manager` | [library-service-api.md](../library-service-api.md) |
| 8 | Staged edits with Save/Cancel; ExifTool embedded XMP writes (no sidecars) verified on readback; one-revision commit with rollback; crop/rotation recipes and derivative export | `metadata.embedded_write`, `editing.metadata_save`, `editing.image_recipes`, `lore.binary_content` | [0007](../decisions/0007-embedded-metadata-writes.md) |

Increment 8 CI evidence: run
[`34595938015`](https://github.com/dtmland/pimio/actions/runs/34595938015)
(commit `b699315`) passed Linux, Windows, and macOS. The Local Linux container
job is excluded from CI and must be run manually.

Known follow-ups from completed increments (not blocking their status):
crop has a session API but no rubber-band control; derivative export has no
dedicated dialog; manual field-notes entries for Save are not yet in
[manual-testing.md](manual-testing.md).

## Remaining work

### Increment 9 — Timestamp repair and organization — Not started

Batch timestamp shift, ordered redistribution, drag/drop reordering,
timezone suggestions, and related groups. Organization today is scan/watch
plus metadata search and filtering.

### Increment 10 — Video playback, trim, and scene suggestions — Not started

Video today is thumbnail/frame extraction only. Missing: playback adapter,
trim recipe application, stream-copy eligibility, scene suggestions.

### Increment 11 — Basic location — Not started

Existing GPS is read. Missing: manual assignment/correction UI, undo, and an
optional map-provider adapter.

### Increment 12 — Resilience, performance, packaging, RC — In progress

Landed: unsigned release archives from `.github/workflows/release.yml` with
Qt, LORE, and ExifTool redistribution, launchers, `pimio-doctor`, layout
verification, and packaged offscreen launch smoke on all three platforms;
shared packaging assets under `packaging/`; extensive fault and recovery
coverage from earlier increments.

Still required for Complete:

- Library-health UI for roots, watchers, jobs, conflicts, disk space, and cache.
- Resource limits and measured large-library responsiveness with published
  results.
- Completed dependency/legal review against
  [../dependency-bom.md](../dependency-bom.md) (including HEVC patent and
  notice obligations) and platform policy confirmation in
  [../supported-platforms.md](../supported-platforms.md).
- Full manual platform checklist sign-off. Signing and notarization remain
  separate release operations.
- Resolution of the promotion alpha risk, or an explicit decision to ship 1.0
  with it (see [lore-0.14-upgrade.md](lore-0.14-upgrade.md)).
