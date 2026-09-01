# pimio 1.0.0 Implementation Plan

This document turns [the v1 release plan](pimio-v1.md) into independently
verifiable implementation increments. It is intentionally not a promise to
implement all of v1 in one branch or agent session.

## Reorientation: the Library-Centric Direction

The plans were reoriented around the principle that **one pimio Library is
one LORE repository** with a stable identity, portable as a self-contained
unit, and the foundation for the v2 server / v3 studio progression described
in [pimio.md](pimio.md) and [pimio-v2.md](pimio-v2.md). Increments 0–7.6 were
implemented before this reorientation. This section records where the
implementation stands against the new direction and what the delta is; the
delta work is captured as Increments 7.7–7.9 and amendments to Increment 8.

### Current state versus the target design

| Target requirement | Current state | Delta |
| --- | --- | --- |
| Storage abstraction (LORE behind one adapter) | **Done.** `pimio::core::DurableStore`; only `src/lore/` knows LORE exists | None |
| Asset identity independent of path/filename | **Done.** `core::MediaId` survives move/rename; duplicates get distinct ids | None |
| Canonical vs. derived separation, rebuildable indexes | **Done.** SQLite projection and thumbnail cache are disposable and rebuilt from the store; enforced by tests | Keep enforcing for every new index |
| Immutable, append-only history | **Done at the store level.** LORE revisions are append-only; `commit()` is a durability boundary | None |
| Library identity independent of location | **Missing.** A "library" is a hash of the `--library` paths; the repository carries no identity | Increment 7.7 |
| Revision identity with author, app version, parent | **Partial.** `Checkpoint` has id, message, timestamp only | Increment 7.7 |
| Author identity on every revision | **Missing.** No author concept anywhere | Increment 7.7 |
| Authorization boundary in the service layer | **Missing.** No permission concept | Increment 7.7 (conceptual only) |
| Explicit original→derivative relationships | **Partial.** Thumbnails are correctly derived/disposable, but edited versions and exports have no modeled relationship | Increment 8 (amended) |
| Originals stored/versioned in the repository | **Not the current model.** The repository stores JSON records; originals stay in user folders | Increment 7.8 gate |
| Library lifecycle (create/open/rename/move/backup/restore) | **Missing.** Only repeatable `--library <path>` CLI options | Increment 7.9 |
| Service API boundary remotable in v2 | **Partial.** Services are UI-independent C++ interfaces, but no consolidated session API designed for a future network boundary | Increment 7.9 records the boundary; v2 implements it |
| Non-destructive edit recipes | **Planned as designed.** Increment 8 already specifies versioned recipes | None |
| Single-writer coordination above LORE | **Done.** Writer lock per store; matches the rule that future multi-process/multi-user features coordinate above LORE | None |

### Design holes and recommendations (for review)

1. **Managed versus referenced originals.** The target design describes a
   library that *contains* originals and derivatives, making it a
   self-contained portable unit. The implementation references media in
   place and versions only metadata/recipes. LORE's measured performance
   covers ~2 KB JSON records, not multi-gigabyte video. **Recommendation:**
   keep the current *referenced* model as the v1 default, and run the
   Increment 7.8 feasibility gate for a *managed* mode (originals committed
   to the repository). Until managed mode exists, "backup library" must
   explicitly include the referenced media roots, or the portability claim
   is false. Record the outcome as decision 0005.
2. **The LORE branch-advance defect remains a release blocker** (condition 4
   of [decision 0001](../decisions/0001-lore-durable-store.md)). The
   library-centric direction makes the repository the *only* durable copy of
   organizational state, which raises, not lowers, the bar.
3. **Synchronization semantics are unproven.** The v2 mirror/synchronize
   mode must be scoped to what LORE actually supports; pimio must not imply
   Git-style peer-to-peer sync. **Recommendation:** a sync-semantics spike is
   the entry gate for that v2 feature, mirroring how Increment 2 gated
   persistence.
4. **History shape for future multi-user.** Per decision 0001, concurrent
   writers corrupt LORE; coordination happens above it. **Recommendation:**
   plan v3 collaboration as server-serialized linear history per library
   (parallel *revisions of an asset* are an application concept), rather
   than assuming storage-level branching/merging.
5. **Albums, tags, and ratings must be canonical.** When they are
   implemented, they belong in repository records, not only in the
   projection, or a rebuilt library silently loses organization.
6. **Users are records, not configuration.** The single implicit v1 user
   should be represented as a stable authored identity stored with the
   library so v3 can add named users without rewriting history.

## Planning Review and Recommendations

The product direction is coherent, but the current v1 scope is too broad for a
single implementation effort. Indexing, durable versioned storage, portable
metadata writes, image editing, video editing, maps, inference, and three
native platforms each carry separate correctness and distribution risks.

Before feature work, resolve these plan-level issues:

1. **Prove LORE first.** Its API, availability, packaging, data model, locking,
   external-change detection, and recovery behavior are not yet defined. Build
   a disposable spike that saves and reloads representative metadata and edit
   recipes. If LORE cannot meet the acceptance gate, stop and revise the v1
   storage decision rather than silently making SQLite authoritative.
2. **Define the supported platform policy.** Record the minimum Windows,
   macOS, and Linux versions, CPU architectures, filesystems, and Qt version.
   CI runner images are test environments; they do not by themselves define
   the supported product matrix.
3. **Choose dependency paths with small spikes.** In particular, decide between
   Qt Multimedia and a separately integrated FFmpeg/libmpv playback path, and
   validate metadata-write, RAW, map-provider, and deployment behavior. Keep
   these behind interfaces until the decisions are recorded.
4. **Create a dependency bill of materials before integration.** Record exact
   versions, enabled features, runtime or process boundary, licenses, notices,
   source, and redistribution status. Treat legal review as a release gate, not
   a test that CI can certify.
5. **Use owned test media.** Commit only small generated or explicitly licensed
   fixtures. Keep a separately managed large-library corpus for performance and
   soak tests.
6. **Keep v1 organization-first.** Face recognition, landmark inference,
   advanced map behavior, sync, and a custom renderer remain v2 work. Scene
   detection and related-group suggestions may ship as clearly optional
   analysis only after core recovery and editing paths are dependable.
7. **Specify destructive-operation rules.** Tests must establish that scans,
   metadata changes, failed saves, cancellation, and exports do not alter an
   original unless the user explicitly selected a supported metadata write.

## Rules for Every Implementation Session

Each session should select one increment below and:

- restate its in-scope requirements and exclusions;
- avoid opportunistic work from later increments;
- add or update automated tests with the implementation;
- run the repository's configure, build, and CTest commands locally;
- leave all existing required GitHub Actions checks green;
- attach logs or fixtures needed to diagnose a failure;
- record the new status in [progress.md](progress.md); and
- update this document only when an accepted requirement or decision changes.

An increment is complete only when its listed evidence exists. A build that
compiles without executing the stated tests is not complete.

## Increment 0 — Project Skeleton and Observable CI

**Deliverables**

- CMake and CMake Presets project using one pinned Qt 6 minor release.
- Minimal C++ core library, Qt application shell, and Qt Test executable.
- Repository conventions for source, tests, fixtures, and generated output.
- GitHub Actions build-and-test matrix described below.
- Initial dependency bill of materials and supported-platform policy stubs.

**Automated acceptance**

- A core unit test and an offscreen application smoke test execute through
  CTest.
- Configure, compile, and tests pass on Linux, Windows, and macOS.
- A deliberately failing test is temporarily verified to fail each matrix job
  before the change is removed.

## Increment 1 — Stable Contracts and Test Corpus

**Deliverables**

- UI-independent media identity, metadata, edit-recipe, job, and error types.
- Abstract storage, metadata, media-request, clock, and filesystem boundaries.
- Small generated fixtures covering JPEG, PNG, RAW-preview simulation, video,
  malformed media, sidecars, and timestamp edge cases.

**Automated acceptance**

- Serialization round trips preserve version and unknown-field behavior.
- Contract tests run against fakes without a display or network.
- Fixture provenance and expected hashes are checked in CI.

## Increment 2 — LORE Feasibility Gate

**Deliverables**

- A replaceable LORE adapter spike for metadata, tags, timestamp changes, edit
  recipes, and explicit user checkpoints.
- Documented process/API boundary, locking, failure, upgrade, packaging, and
  external-change behavior.
- A recorded go/no-go decision. The spike need not become production code.

**Automated acceptance**

- Save, restart, reload, history, failed-save recovery, and external-change
  tests pass using a temporary repository.
- Concurrent or interrupted access never reports an uncommitted change as
  committed.
- Deleting the disposable SQLite projection does not lose committed state.

Do not begin production persistence work until this gate passes or the release
plan is explicitly revised.

## Increment 3 — SQLite Projection, Migrations, and Jobs

**Deliverables**

- Versioned SQLite schema, migration runner, WAL configuration, transactions,
  and rebuildable projections.
- Persistent priority job queue with retries, cancellation, progress, and
  bounded concurrency.

**Automated acceptance**

- Empty, current, previous-version, interrupted-migration, and corrupt-cache
  cases have integration tests.
- Jobs survive restart, run once logically, respect priority, and recover from
  cancellation and injected failures.
- Rebuilding from the durable-store fixture produces deterministic query
  results.

## Increment 4 — Incremental Scan and Media Identity

**Deliverables**

- Multiple library roots, incremental traversal, stable identity, content
  fingerprints, move/rename recognition, and unavailable-root reporting.
- No thumbnail generation or UI beyond diagnostic query output.

**Automated acceptance**

- Temporary-directory tests cover add, update, rename, move, delete, duplicate,
  symlink policy, permission failure, disappearing files, and restart.
- A repeated unchanged scan performs no logical updates.
- A generated large tree has a recorded benchmark and does not exceed the
  agreed memory/concurrency bounds.

## Increment 5 — Metadata Read, Query, and Search

**Deliverables**

- Image and video metadata extraction behind the chosen adapter.
- Chronological queries, filters, pagination, and full-text search.
- Explicit precedence and conflict records for embedded metadata and sidecars.

**Automated acceptance**

- Golden tests cover capture-time precedence, timezone presence/absence,
  rotation, GPS, camera fields, duration, audio presence, malformed values, and
  Unicode search.
- Query ordering is deterministic when timestamps are equal or missing.
- Unsupported media becomes a visible error record rather than crashing or
  blocking the scan.

## Increment 6 — Thumbnails, Models, and Basic Browser

**Deliverables**

- Persistent fingerprint-keyed image and video thumbnail requests.
- Cancellation, bounded caches, visible-item priority, and a virtualized QML
  grid/timeline backed by the stable query model.
- Progressive detail preview and clear placeholders/errors.

**Automated acceptance**

- Unit tests verify cache keys, invalidation, eviction, priority, cancellation,
  and corrupt-cache regeneration.
- QML smoke tests create the grid and detail view, scroll a synthetic model,
  and request only the expected visible range plus prefetch margin.
- Linux additionally runs the smoke test under Xvfb; offscreen smoke tests run
  on all three platforms.

## Increment 7 — Watching and Reconciliation

**Deliverables**

- Coalesced filesystem events converted to durable jobs.
- Overflow/missed-event reporting and low-priority reconciliation.
- Small platform adapters with a portable behavioral contract.

**Automated acceptance**

- Contract tests inject create, rename, burst, duplicate, reordered, overflow,
  and dropped events.
- Native integration tests execute on every platform runner.
- Reconciliation converges to the same index as a clean scan.

## Increment 7.5 — Browsing Controls and Settings

Inserted after Increment 7 rather than renumbering 8–12, which are referenced by
number across the plans, decision records, and progress log. It is deliberately
small and is pulled forward of Increment 8 because the increments after it are
judged by looking at a library — ordering, thumbnail quality, timestamps — and
that judgement needs controls a person can drive.

**Deliverables**

- A `pimio::settings` component holding every user-visible setting, split into
  **stored settings** persisted to an application-wide `pimio.conf` and
  **session settings** that reset at every launch.
- Sorting in the projection database by capture time, file name, file date,
  file type, and file size, in either direction, exposed as a control in the
  browser.
- Keyboard navigation: arrows and PageUp/PageDown in the grid, left/right in
  the preview following the grid's order, with a held key accelerating to a
  bounded maximum step.
- Wheel scrolling at a configurable speed with optional acceleration within one
  gesture.
- A live tile-size slider bounded by the thumbnail resolutions the model can
  request, and a settings dialog reachable from the browser toolbar.

**Automated acceptance**

- Settings tests pin every default, the clamping of out-of-range values, the
  tolerance of a corrupt configuration file, and that session settings are not
  written to disk.
- Projection tests cover each sort key in both directions, ties, records with
  no file date, and files with no extension.
- Model tests cover re-sorting, an unknown sort key, tile-size-to-thumbnail
  tier selection, and re-requesting the visible window after a size change.
- QML smoke tests drive arrow/page keys, key-hold acceleration, wheel scrolling
  at two speeds, the tile-size binding, preview stepping, and the settings
  dialog.

## Increment 7.7 — Library, Revision, and Author Identity

Foundation work from the reorientation above. Small, code-level, and blocking
for Increment 8, because checkpoints written by Save must already carry
provenance.

**Deliverables**

- A library descriptor stored as a reserved record inside the repository:
  stable library id (generated once at creation), user-visible name, format
  version, and creation timestamp. The id survives copy, backup, restore,
  move, and rename; the on-disk path is never the identity.
- `core::Checkpoint` extended with author (a stable user id), application
  version, and parent-checkpoint linkage. v1 creates one implicit local user,
  stored with the library, so future named users do not rewrite history.
- A documented conceptual authorization model in the service layer (read /
  write / administer / share per library), satisfied in v1 by granting the
  implicit user everything. No permission UI.
- The application index/cache directory keyed by library id rather than by a
  hash of the `--library` paths.

**Automated acceptance**

- A library copied or moved to a new path reports the same library id; two
  independently created libraries report different ids.
- Serialization round trips preserve the new checkpoint fields, and
  pre-extension repositories load with sensible defaults (unknown author),
  proving forward/backward compatibility.
- History reports author, application version, and parent for new
  checkpoints.

## Increment 7.8 — Library Storage-Model Gate (Managed Originals)

A feasibility gate in the spirit of Increment 2. The target design describes
a library that contains original media; the implementation references media
in place. Decide whether LORE can carry original photo/video content.

**Deliverables**

- A disposable spike committing representative original media (including
  multi-hundred-MB video) into a LORE repository; measured commit time,
  repository size, deduplication behavior, and read-back cost.
- A recorded go/no-go decision (decision record 0005) choosing between:
  managed libraries (originals in the repository), referenced libraries
  (current model, with backup explicitly covering the media roots), or both
  as user-selectable modes.
- Documented consequences for backup/restore and the portability claim in
  either outcome.

**Automated acceptance**

- Spike tests demonstrate commit, restart, reload, and integrity of binary
  content, or the decision record documents the failure that produced a
  no-go.

Do not implement managed-mode ingest before this gate passes; the referenced
model remains the default and is not blocked by this increment.

**Outcome:** Complete — **no-go for managed originals in v1**. LORE 0.8.5
round-tripped and deduplicated a 256 MiB binary payload, but the checkout plus
immutable store roughly doubles original-media storage and pimio's required
pre-commit recovery backup scales with the complete `.lore` corpus. v1 keeps
referenced originals and requires backups to include or explicitly exclude
their media roots. See
[decision 0005](../decisions/0005-managed-versus-referenced-originals.md).

## Increment 7.9 — Library Manager and Lifecycle

Makes the Library a user-facing first-class object rather than a CLI flag.

**Deliverables**

- Create, open, close, and switch libraries from the application, with a
  Library Manager listing known libraries by name and identity; location is
  displayed but is not the identity.
- Rename and move a library; back up a library to a single archive and
  restore it, reconstructing the projection, job queue, and thumbnail caches
  from the repository. In the referenced model the backup includes or
  clearly enumerates the media roots.
- A documented in-process service API boundary (session/service interfaces
  the UI consumes) shaped so v2 can place a network between client and
  services without redesign. No networking, authentication, or user
  management is implemented.

**Automated acceptance**

- Create/open/switch tests cover fresh, existing, missing, and locked
  libraries.
- A backup/restore round trip on a populated library preserves the library
  id, records, history, and organization, and rebuilds all derived state.
- Restoring onto a machine path different from the original produces a
  working library recognized as the same library.

## Increment 8 — Save, Portable Metadata, and Image Recipes

**Deliverables**

- Staged metadata edits and explicit LORE-backed Save/checkpoint behavior.
  Checkpoints carry the author, application version, and parent linkage from
  Increment 7.7.
- Conflict-aware XMP/embedded writes using atomic replacement where supported.
- Versioned crop/orientation/rotation recipes and image export.
- Explicit original→derivative relationships recorded in the media record:
  an export or rendered edit is linked to its source asset and recipe
  revision rather than being inferred from filenames. (Thumbnails/previews
  remain fingerprint-keyed disposable cache entries, not records.)

**Automated acceptance**

- Byte-for-byte original-preservation tests cover preview, cancel, failed save,
  crop/rotate recipes, and export.
- Metadata round trips are verified by rereading output with the production
  adapter and an independent compatibility tool where practical.
- Fault injection covers no space, permission loss, process interruption, and
  sidecar races without losing the prior valid file.

## Increment 9 — Timestamp Repair and Organization Workflows

**Deliverables**

- Batch timestamp shifts, ordered redistribution, drag/drop reordering, and
  confidence-bearing timezone suggestions.
- Related-time/location group suggestions that never modify metadata silently.

**Automated acceptance**

- Table-driven tests cover DST gaps/folds, leap days, missing zones, ambiguous
  locations, mixed selections, undo-before-save, and reload-after-save.
- QML interaction tests verify preview, confirmation, cancellation, and
  accessibility of uncertainty messages.

## Increment 10 — Video Playback, Trim, and Scene Suggestions

**Deliverables**

- Playback adapter with software fallback, video detail view, trim recipe, and
  export path.
- Stream-copy eligibility reporting and optional scene suggestions.

**Automated acceptance**

- Generated clips cover rotation, audio/no-audio, variable frame rate,
  unsupported codecs, black opening frames, keyframe-aligned and unaligned
  trims, cancellation, and corrupt input.
- Output duration and selected streams are probed; stream-copy claims are
  verified rather than inferred from file extension.
- Playback initialization and software fallback smoke tests run on all three
  platform runners. Hardware acceleration remains a manual hardware test.

## Increment 11 — Basic Location

**Deliverables**

- Existing-GPS display and manual assignment/correction for images and videos.
- Optional map-provider adapter with non-blocking offline/error behavior.

**Automated acceptance**

- Coordinate validation, precision, metadata round trip, undo, provider
  timeout, no-network, and malformed-response tests pass.
- The application remains usable when the map component cannot initialize.

## Increment 12 — Resilience, Performance, Packaging, and Release Candidate

**Deliverables**

- Library-health UI for roots, watchers, jobs, conflicts, disk space, and cache.
- Resource limits and measured large-library responsiveness.
- Unsigned installable artifacts for each supported platform; signing and
  notarization are separate controlled release operations.
- Completed dependency/legal review and manual platform checklist.

**Automated acceptance**

- Fault tests cover restart, cache deletion/corruption, low disk, unavailable
  roots, event loss, unsupported media, failed jobs, and metadata conflicts.
- Performance tests publish results without hiding functional failures; agreed
  regressions become blocking only after stable baselines exist.
- Clean-runner install/package smoke tests launch the packaged app and archive
  logs and artifacts.

## GitHub Actions Verification Design

Implement CI in Increment 0 rather than waiting for features.

### Pull-request workflow

Create a matrix with `fail-fast: false` and three explicitly named jobs:

- `Build and test (Linux)` on the selected pinned Ubuntu runner;
- `Build and test (Windows)` on the selected pinned Windows runner; and
- `Build and test (macOS)` on the selected pinned macOS runner.

Each job must check out the same commit, restore only version-keyed caches,
install the pinned Qt/modules and declared native dependencies, run the shared
CMake preset, build, and execute `ctest --output-on-failure`. Avoid
`*-latest` labels once the supported runner images are chosen, because silent
image changes make failures harder to reproduce.

Use the same CMake targets and CTest names locally and in CI. Platform-specific
setup belongs in presets or small workflow steps, not in separate unverified
build systems. Do not mark matrix jobs `continue-on-error`.

### Test layers

| Layer | Linux | Windows | macOS | Trigger |
| --- | --- | --- | --- | --- |
| Core unit/contract tests | Required | Required | Required | Every PR/push |
| Native adapter integration | Required | Required | Required | Every PR/push |
| Qt offscreen smoke | Required | Required | Required | Every PR/push |
| X11 via Xvfb | Required | N/A | N/A | Every PR/push |
| Wayland via a headless compositor | Required when stable | N/A | N/A | Scheduled/release |
| Sanitizers and static analysis | Required | Optional | Optional | PR or scheduled |
| Packaging/install smoke | Required | Required | Required | Main/release |
| Real GPU, signing, installer UX | Manual/self-hosted | Manual/self-hosted | Manual/self-hosted | Release |

GitHub-hosted macOS and Windows runners can compile and execute native code, but
they do not prove real desktop, GPU, camera-codec, signing, Gatekeeper, or
SmartScreen behavior. Those checks need documented manual tests or labeled
self-hosted runners; they must not be represented as covered by headless CI.

### Making results visible and enforceable

- Publish CTest JUnit output, application logs, and failure screenshots as
  artifacts even when a test step fails.
- Use stable job names and configure branch protection to require all three
  build-and-test jobs before merge.
- Add the workflow status badge to the README after the workflow exists.
- Keep the Actions matrix expanded so the PR Checks page visibly reports Linux,
  Windows, and macOS independently.
- Add a concurrency group that cancels superseded runs for the same PR while
  preserving release runs.
- Use scheduled runs for longer reconciliation, performance, sanitizer,
  Wayland, and packaging suites, and make release creation depend on their most
  recent successful results.

### Per-increment completion evidence

Every implementation pull request should identify its increment, list the
specific CTest tests that establish acceptance, and link the successful
three-platform workflow run. If a requirement is manual, record the platform,
OS version, hardware, steps, and result in the release checklist. A skipped or
allowed-to-fail platform is an explicit incomplete increment, not a passing
one.
