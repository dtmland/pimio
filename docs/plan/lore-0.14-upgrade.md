# LORE 0.14.0 upgrade plan

**Status:** Implemented. pimio pins LORE **0.14.0** (released 2026-11-17),
the latest release as of 2026-11-26. Phases 1–7 are done and validated on
local Linux x86-64. CI on Linux, Windows, and macOS, the Release workflow, and
the local container and Windows Sandbox harnesses still need to confirm it
(phase 8). The measured outcomes are in [Measured outcome](#measured-outcome).

This plan covers moving every build context from 0.9.0 to 0.14.0, getting the
complete test suite green, and deciding which pimio workarounds for LORE
defects can be removed. The defects themselves are tracked in
[lore-upstream-issues.md](lore-upstream-issues.md). It follows the same pattern
as the 0.8.5→0.9.0 move (Increment 7.8a in
[pimio-v1-implementation.md](pimio-v1-implementation.md)).

## Why go straight to 0.14.0

Releases since the pin are 0.10.0 (09-18), 0.10.1 (10-06), 0.11.0 (10-22),
0.12.0 (10-27), 0.13.0 (11-06), and 0.14.0 (11-17).

- Several fixes pimio needs land across those releases:
  - fan-out marker damage (0.10.0)
  - push retry after a missing fragment (0.10.0, 0.11.0)
  - local-store durability on interrupted flushes (0.11.0)
  - cross-process deadlocks (0.11.0, 0.12.0)
  - local-store content lost after concurrent reads (0.14.0)
- Stopping at an intermediate release would mean doing the C ABI rebuild and
  the test-allowance review more than once.
- 0.11.0 requires every process sharing a store, and both client and service,
  to run the same build. Our pin already ships `liblore`, `lore`, and
  `loreserver` together, so one jump keeps them consistent.

If 0.14.0 shows a blocking regression, fall back to the newest release that
passes. Do not mix versions.

## Release-note review: do the new versions resolve our issues?

This section summarizes the release notes for 0.10.0 through 0.14.0. The
detailed table and links are in
[lore-upstream-issues.md](lore-upstream-issues.md). All five pimio-filed issues
(#187, #188, #190, #191, #192) are still **open** upstream with no maintainer
response.

| pimio issue | Release-note verdict | Expected test outcome on 0.14.0 |
| --- | --- | --- |
| Fan-out `Address not found` | **Fixed (0.10.0).** Every bucket write outside the two-phase commit now commits the group's level marker | Windows corrupt-checkout test and the post-mismatch commit pass with no allowance |
| #187: short `level.pending` | **Not named.** 0.11.0 claims that interrupted flushes and index reorganizations no longer lose data | Unknown. Keep the open-time repair. The kill sweep reports whether repair still happens |
| Killed-commit revision/index mismatch | **Not named.** Same 0.11.0 durability claim | Unknown. The allowance is gated to 0.9.0, so any recurrence fails hard and has to be triaged |
| #188: commit not durable without flush | **No contract change** | Keep flush-before-acknowledge. It is part of pimio's contract regardless |
| #190: push accepts mismatched ID | **Not mentioned.** Only adjacent server write checks were tightened in 0.11.0 and 0.13.0 | Expect the mismatched push still to be accepted. pimio's preflight stays |
| #191: interrupted initial push retry | **Plausibly fixed:** 0.10.0 stops the client from recreating a branch it misread as missing, and 0.11.0 fixes retried pushes failing on a missing fragment | Verify. If the retry succeeds and the clone is byte-complete, remove the alpha risk |
| #192: attach remote API | **Not added** | Keep the `config.toml` workaround. The "no public setter" assertion should still hold |

The notes are claims; the tests are the evidence. The table above is the
pre-upgrade prediction and is kept for the record. The measured results are
below.

## Measured outcome

| pimio issue | Result on 0.14.0 | What changed in pimio |
| --- | --- | --- |
| Fan-out `Address not found` | **Fixed** | QSKIP, promotion tolerance, and helper removed |
| #187: short `level.pending` | Not observed (local Linux sweeps) | Open-time repair kept |
| Killed-commit revision/index mismatch | Not observed (local Linux sweeps) | 0.9.0 allowance removed; recurrence now fails |
| #188: commit not durable without flush | Unchanged contract | Flush-before-acknowledge kept |
| #190: push accepts mismatched ID | **Still present** | Preflight kept; `QEXPECT_FAIL` gated to 0.14.0 |
| #191: interrupted initial push retry | **Still present** (`Branch main already exists`) | `QEXPECT_FAIL` gated to 0.14.0; alpha warning kept with updated version |
| #192: attach remote API | **Still missing** | `config.toml` workaround kept |

Other findings from implementation:

- The adapter needed no source changes. Every `lore.h` field and event pimio
  uses is unchanged; removed fields (`no_atime`, `revision_commit_args.stats`)
  were never used.
- The test-harness `repository create` calls did not need rewriting: offline
  creation with a URL still works in 0.14.0.
- An invalid user-level LORE configuration does break every call, so CTest
  isolates it with `LORE_GLOBAL_PATH`.
- Names Windows cannot store are silently left unstaged rather than refused.
  Managed originals never use source names, which a new test proves.
- Corruption tests still detect damage without LORE's read-time hash check.

## Impact of upstream changes on pimio

### C API (`liblore`, loaded with `QLibrary`, compiled against `lore.h`)

- **ABI changes that require a rebuild:**
  - `lore_event_t` gains `LORE_EVENT_FILE_AUDIT` (0.11.0).
  - Push events now begin with repository and branch (0.14.0).
  - Global arguments lose `no_atime` (0.10.0) and gain `trust_remote`
    (0.11.0). The whole struct, padding included, must be zeroed.
  - pimio already zeroes every argument struct and never sets `no_atime`. The
    only work here is to rebuild against the new header and re-check every
    field name the adapter sets.
- **Empty strings arrive as NULL** (0.10.1). pimio's string conversion already
  treats NULL and zero length as empty. Keep that covered by a test.
- **Error codes renumbered** (0.14.0: NotAuthenticated 16, NotAuthorized 17).
  pimio maps failures by message text, not code, but logs the raw status.
  Check that nothing compares a literal status value.
- **`repository create` syncs to disk only when `sync_data` is set** (0.11.0).
  pimio already sets `sync_data` in its global arguments. Confirm the local
  create path, which passes a placeholder URL while offline, still behaves.
- **Calls after `lore_shutdown` return ShutDown (193)** (0.10.0). Confirm no
  teardown order in pimio or its tests calls LORE after shutdown.
- **Local reads no longer verify content hashes** (0.11.0, ADR-00023).
  Corrupt-checkout and corrupt-store tests may now see wrong bytes instead of
  an error. Each such test must still prove that pimio detects or recovers the
  damage. Do not weaken them.
- **Windows-reserved file names are refused on every platform** (0.13.0).
  Managed originals keep their source names, so a Linux or macOS user with a
  file named like `CON.jpg`, `name.`, or `a:b` would now fail to import. Add a
  test and decide whether to show a clear error or store under a sanitized
  repository path.
- **A version is reported as `<ver>+local` unless the build stamps it**
  (0.10.1). pimio reports the header's interface version. Check that the
  "about"/doctor output is still accurate.

### LORE CLI used by tests

- `repository create` under `--offline` no longer takes a URL. It names the
  repository after the current directory, and a bare name is no longer
  expanded from `LORE_REMOTE_URL` (0.10.0). Both test calls that create a
  repository with an explicit URL or name in `tst_lore_server_promotion.cpp`
  must be rewritten. They will probably fail first.
- Partial revision hashes are refused (0.10.0). Check every history or
  checkout call in the test support helpers.
- An invalid user-level `config.toml` now stops every command (0.14.0). Tests
  must isolate LORE's user configuration so a developer's machine cannot break
  them.
- `--dry-run` is side-effect free (0.12.0). `.loreignore` uses gitignore
  semantics (0.13.0). `sync --reset` leaves untracked files (0.13.0). None of
  these is used today. Confirm with a grep.
- The new `lore clone --offline/--cache` (0.11.0) may change how much history
  a promotion clone hydrates. Re-check the lazy-history assertions.

### Data compatibility

pimio is alpha and makes no compatibility promise for saved data
([policy](../conventions.md#alpha-data-compatibility-policy)). This upgrade
therefore adds **no migration path** from libraries written with 0.9.0 (or
0.8.5) and no downgrade handling. Stores created by earlier builds, including
any damaged by the fan-out bug that 0.10.0 does not repair, are simply
recreated.

- Retire `lore.migration` and its 0.8.5 fixture
  (`tests/lore/fixtures/lore-0.8.5-store.tar.gz`) instead of extending them.
  If 0.14.0 happens to open the old fixture, that is not a reason to keep it.
- Note in the change description that older alpha libraries and
  `.pimio-backup` files may not open after the upgrade.

### Artifacts and platforms

- Archive naming (`<bundle>-v<version>-<triple>.<ext>`) is unchanged, so only
  the version and checksum table change in `cmake/PimioLore.cmake`.
- 0.14.0 adds a portable `aarch64-unknown-linux-gnu` triple. 0.10.0 notes say
  the existing `neoverse-512tvb` build can crash with SIGILL on older arm64
  CPUs. Plan to switch pimio's Linux arm64 host triple to the portable build,
  and update [supported-platforms.md](../supported-platforms.md).
- 0.14.0 also ships `-debug` variants and an MSI. Neither is needed.

## Implementation phases

Each phase ends with a pushed checkpoint commit, following
`.github/copilot-instructions.md`.

1. **Pins.** Change the version and replace the checksum table in
   `cmake/PimioLore.cmake`. Take the digests from the downloaded artifacts, and
   cross-check them against the release's published SHA-256 values. Switch the
   Linux arm64 triple. Run `python -m unittest discover -s tests/build -v`.
   The workflows, `pins.sh`/`pins.ps1`, and `tools/local-build/` read the pin
   and need no literal changes. Their cache keys change automatically.
2. **Adapter rebuild.** Build against the new `lore.h`, then fix compile or
   field-name breaks in `src/lore/src/lore_api.*` and the event handlers.
   Run `lore.adapter`, `lore.projection`, and `lore.binary_content`.
3. **Test-harness CLI fixes.** Update repository creation and any revision
   arguments in the LORE test support and promotion tests. Isolate the user
   configuration.
4. **Retire migration coverage.** Remove `lore.migration`, its 0.8.5 fixture,
   and the CMake wiring for them. Do not add a 0.9.0 fixture.
5. **Version-gated allowances.** Every allowance is an exact `"0.9.0"` match,
   so the new pin turns each one into a real assertion. For each, record the
   result in the issue tracker:
   - Fan-out QSKIP (`tst_lore_faults.cpp`) and tolerance
     (`tst_lore_server_promotion.cpp`): expected to pass. Delete them along
     with the `hasUnmarkedFanOutGroup` helper if it has no remaining use.
   - Killed-commit observational allowance and helpers
     (`tst_lore_faults_process.cpp`): run the kill sweep **repeatedly on all
     three OSes**, at least the CI fault matrix plus several extra re-runs.
     Delete the allowance if it stays clean. If it recurs, minimize it and
     comment on #187.
   - Mismatched-ID push `QEXPECT_FAIL`: keep it, re-gated to the new version,
     if the push is still accepted. Otherwise remove it and note the fix on
     #190.
   - Interrupted initial-push `QEXPECT_FAIL`, currently unconditional: gate it
     to the pinned version. If the retry now succeeds and the clone is
     byte-complete, remove the expected failure, the alpha warning in
     `PromotionDialog.qml`, the comment in `lore_durable_store.h`, and the
     README alpha caveat. Then comment on #191.
   - "No public config setter" assertion: if 0.14.0 adds one, adopt it in
     place of the `config.toml` edit and comment on #192.
6. **Reserved names and hash verification.** Add a test for importing a
   Windows-reserved file name. Re-check that the corrupt-data tests still prove
   pimio-visible detection without LORE's read-time hash check.
7. **Documentation.** Update the references to 0.9.0:
   - `dependency-bom.md`, `supported-platforms.md`, `library-model.md`
   - decisions 0001, 0005, and 0006 (append an outcome note; do not rewrite
     history)
   - `lore-upstream-issues.md`
   - this plan's status
   - `progress.md`
   - `build-architecture.md`, if the triple selection changes
8. **Validation.** CI on Linux, Windows, and macOS, plus the Release workflow
   on a test tag or by manual dispatch. Run `tools/local-build/linux/build.sh`
   manually with a freshly built image. Run the Windows local build and its
   PowerShell 5.1 bootstrap tests, with a fresh Windows Sandbox run if one is
   available. Finish with the repository health metrics.

## Acceptance criteria

- Every build context resolves LORE 0.14.0 through the single pin, and the
  build-contract tests pass.
- The full CTest suite, including `lore.faults` and `lore.server_promotion`,
  passes on Linux, Windows, and macOS with LORE required,
  not skipped.
- No test allowance names 0.9.0. Every remaining allowance is gated to 0.14.0
  and cited in [lore-upstream-issues.md](lore-upstream-issues.md).
- Libraries created fresh under 0.14.0 pass the full suite. Compatibility
  with libraries from earlier alpha builds is out of scope.
- The tracker and this plan record the measured outcome for every issue in
  the review table.

## Risks and rollback

- **The kill sweep is statistical.** A clean run is not proof. Record the
  attempt counts per OS. Keep the open-time marker repair regardless, because
  it is cheap and visible.
- **A new failure shows up in an older code path** (deadlock fixes, last-access
  tracking, nested-repository walks). Minimize it and file it upstream. If it
  blocks, try the previous release (0.13.0). Do not add new tolerances.
- **Rollback** is a single revert of the pin commit plus its test and
  documentation changes. The pin lives in one file.

## Build-change checklist for the implementing PR

Per `.github/copilot-instructions.md`, the implementing PR must include the
cross-context impact matrix, placement review, pin drift check, and shared-first
decision. The expected result:

| Context | Expected change |
| --- | --- |
| CI (`ci.yml`) | No literal edits. The cache key follows the pin |
| Release (`release.yml`) | No literal edits. The cache key and redistribution follow the pin |
| Local Linux (`pinned.sh`, `Containerfile`) | No literal edits. Rebuild the image to validate |
| Local Windows (`pinned.ps1`) | No literal edits. Run the 5.1 bootstrap tests |
| Shared defs (`cmake/PimioLore.cmake`) | Version, checksums, arm64 triple |
| `docs/build-architecture.md` | Update only if the triple selection is described there |
