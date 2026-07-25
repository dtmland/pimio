# pimio 1.0.0 Implementation Plan

This document turns [the v1 release plan](pimio-v1.md) into independently
verifiable implementation increments. It is intentionally not a promise to
implement all of v1 in one branch or agent session.

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
- attach logs or fixtures needed to diagnose a failure; and
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

## Increment 8 — Save, Portable Metadata, and Image Recipes

**Deliverables**

- Staged metadata edits and explicit LORE-backed Save/checkpoint behavior.
- Conflict-aware XMP/embedded writes using atomic replacement where supported.
- Versioned crop/orientation/rotation recipes and image export.

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

