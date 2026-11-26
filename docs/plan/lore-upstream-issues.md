# LORE upstream issues tracked by pimio

This is the living list of LORE defects and gaps that pimio works around. It
replaces the former `lore-0.9-upstream-issue-drafts.md`, whose drafts have all
been filed upstream. For each issue, this page records the upstream link, the
pimio workaround, and the test that will notice when the issue changes. Update
it whenever the LORE pin changes. The most recent upgrade is described in
[lore-0.14-upgrade.md](lore-0.14-upgrade.md).

Pinned version: **LORE 0.14.0** (`cmake/PimioLore.cmake`). Upstream status was
last checked on 2026-11-26 against release notes through **v0.14.0**.

## Summary

| Issue | Upstream | Measured on 0.14.0 | pimio workaround / test hook |
| --- | --- | --- | --- |
| Short `level.pending` blocks repository open after an interrupted write | [EpicGames/lore#187](https://github.com/EpicGames/lore/issues/187) (open) | Not observed. Local Linux kill sweeps interrupted 4 of 6 attempts per run over 25+ runs; none needed marker repair. CI sweeps on Windows and macOS pending | `LoreDurableStore::open` still repairs empty pending markers (cheap and visible); `lore.faults` kill sweep |
| A reported commit is not durable without an explicit flush | [EpicGames/lore#188](https://github.com/EpicGames/lore/issues/188) (open) | Contract unchanged by design | Flush before acknowledging a checkpoint; `killedProcessAfterCommitKeepsTheRevisionItReported` |
| A killed commit leaves revision log and index disagreeing (`1 revision / 26 records` or `2 revisions / 1 record`) | Covered by #187/#188 reports | Not observed in the same sweeps | 0.9.0 allowance **removed**; any recurrence now fails `lore.faults` |
| Delayed flush writes fan-out buckets without their level marker (`Address not found`) | Fixed upstream in [`e9d056fb`](https://github.com/EpicGames/lore/commit/e9d056fb459ed644eff1382f21b034621f8ded42) (0.10.0), no pimio ticket | **Fixed.** Not seen in promotion or corrupt-checkout restore | QSKIP, promotion tolerance, and `hasUnmarkedFanOutGroup` **removed** |
| Push accepts a remote whose repository ID differs | [EpicGames/lore#190](https://github.com/EpicGames/lore/issues/190) (open) | **Still present.** The mismatched push is accepted | `repository info` preflight in `promoteToServer`; `QEXPECT_FAIL` gated to 0.14.0 |
| Interrupted initial push cannot be retried (`Branch main already exists`) | [EpicGames/lore#191](https://github.com/EpicGames/lore/issues/191) (open) | **Still present.** The retry fails with the same message, despite the 0.10.0/0.11.0 push-retry fixes | `QEXPECT_FAIL` gated to 0.14.0 and to that message; alpha disclosure in `PromotionDialog.qml` and the README |
| No public way to attach a remote to an existing local repository | [EpicGames/lore#192](https://github.com/EpicGames/lore/issues/192) (open feature request) | **Still a gap.** `repository config set` does not exist | Atomic `.lore/config.toml` edit; `noRemoteAttachesThroughDocumentedConfig` |
| Branch can advance ahead of its state | Not filed | No. Fixed in 0.9.0 | None needed |

## LORE 0.14 behaviors pimio relies on or guards against

These are not defects, but pimio's tests depend on them:

- **User configuration is global.** An invalid
  `$XDG_CONFIG_HOME/lore/config.toml` (or the platform equivalent) stops every
  command and every `liblore` call; pimio shows the resulting open error. CTest
  sets `LORE_GLOBAL_PATH` to a per-test directory under the build tree
  (`tests/CMakeLists.txt`) so a developer's configuration cannot affect tests.
- **Names Windows cannot store are silently left unstaged** on every platform
  (for example names containing `:` or ending in a dot or space). pimio stores
  managed originals under content-derived names with a whitelisted media
  extension, so source names never reach LORE;
  `lore.binary_content` `reservedSourceNamesImportUnderSafeRepositoryPaths`
  proves it.
- **Local reads no longer verify content hashes** (0.11.0). pimio's own record
  parsing reports corrupt checkout files as `CorruptData`, and the corrupt
  immutable-index test still fails visibly; both pass on 0.14.0.

The release-note predictions made before the upgrade are kept in
[lore-0.14-upgrade.md](lore-0.14-upgrade.md). Treat an issue as resolved only
after the matching test passes against the pin.

## Evidence history

- 0.8.5 era: a process killed between `commit` and `flush` lost the reported
  revision in about 20% of trials. This led to flush-before-acknowledge, as
  recorded in [decision 0001](../decisions/0001-lore-durable-store.md).
- 0.9.0 fault sweep, CI run
  [`33889622960`](https://github.com/dtmland/pimio/actions/runs/33889622960):
  - Linux: 3 of 6 attempts interrupted, no marker repair needed.
  - macOS: 5 of 6 attempts interrupted, 1 marker repair needed.
  - Windows: completed the interrupted-commit invariant.
  - Every repository reopened consistently and accepted a later commit.
- 0.9.0 promotion gate (Increment 7.8b):
  - The known-remote round trip works, and history hydrates lazily.
  - The ID mismatch is detected by pimio's preflight, not by LORE.
  - Attaching a no-remote repository works through `config.toml`.
  - Interrupted initial-push retry fails, and neither `--force`,
    re-registration, nor a recovery branch produced a complete clone.

- 0.14.0 (local Linux x86-64, 2026-11-26):
  - Promotion: #190 and #191 reproduce unchanged; the fan-out failure did not
    occur in 6 runs. Offline `repository create <url>` still works and stores
    the host as `remote_url`.
  - Faults: kill sweep clean over 25+ runs, including under CPU load. One
    unreproduced `lore.faults` failure was seen once under `ctest -j4`; its
    slot was not captured, and 4 further full runs plus 15 repeat-until-fail
    runs were clean. Watch CI for recurrence.

Full reproduction steps are in the upstream issues linked above.

## Maintenance rules

- Every allowance for a LORE defect in a test must be gated on the exact pinned
  version and cite an entry on this page. When the pin changes, an allowance
  that is no longer needed must turn into a hard failure or an unexpected pass,
  not stay silently tolerated.
- When an upstream issue closes, or a release claims a fix, record the version
  here. Remove the workaround only after pimio's own test proves the fix.
