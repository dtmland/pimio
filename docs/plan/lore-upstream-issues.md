# LORE upstream issues tracked by pimio

This is the living list of LORE defects and gaps that pimio works around. It
replaces the former `lore-0.9-upstream-issue-drafts.md`, whose drafts have all
been filed upstream. For each issue, this page records the upstream link, the
pimio workaround, and the test that will notice when the issue changes. Update
it whenever the LORE pin changes. The upgrade in progress is described in
[lore-0.14-upgrade.md](lore-0.14-upgrade.md).

Pinned version: **LORE 0.9.0** (`cmake/PimioLore.cmake`). Upstream status was
last checked on 2026-11-26 against release notes through **v0.14.0**.

## Summary

| Issue | Upstream | Observed on 0.9.0 | What release notes 0.10.0–0.14.0 say | pimio workaround / test hook |
| --- | --- | --- | --- | --- |
| Short `level.pending` blocks repository open after an interrupted write | [EpicGames/lore#187](https://github.com/EpicGames/lore/issues/187) (open) | Yes. One of five killed commits on macOS needed marker repair | Not named. 0.11.0 claims broad local-store durability fixes for interrupted flushes. **Unconfirmed** | `LoreDurableStore::open` repairs empty pending markers; `lore.faults` kill sweep |
| A reported commit is not durable without an explicit flush | [EpicGames/lore#188](https://github.com/EpicGames/lore/issues/188) (open) | Yes, by API design | No change to the API contract. **Assume still present** | Flush before acknowledging a checkpoint; `killedProcessAfterCommitKeepsTheRevisionItReported` |
| A killed commit leaves revision log and index disagreeing (`1 revision / 26 records` or `2 revisions / 1 record`) | Covered by #187/#188 reports | Yes, Linux and macOS | Same as #187. **Unconfirmed** | 0.9.0-only observational allowance in `tst_lore_faults_process.cpp` |
| Delayed flush writes fan-out buckets without their level marker (`Address not found`) | Fixed upstream in [`e9d056fb`](https://github.com/EpicGames/lore/commit/e9d056fb459ed644eff1382f21b034621f8ded42), no pimio ticket | Yes. Windows corrupt-checkout restore and Linux promotion | **Fixed in 0.10.0.** The fix prevents new damage but does not repair stores that were already affected | 0.9.0-only QSKIP in `tst_lore_faults.cpp` and tolerance in `tst_lore_server_promotion.cpp` |
| Push accepts a remote whose repository ID differs | [EpicGames/lore#190](https://github.com/EpicGames/lore/issues/190) (open) | Yes | Not mentioned. **Assume still present** | `repository info` preflight in `promoteToServer`; 0.9.0-gated `QEXPECT_FAIL` |
| Interrupted initial push cannot be retried (`Branch main already exists`) | [EpicGames/lore#191](https://github.com/EpicGames/lore/issues/191) (open) | Yes. Promotion ships as an accepted alpha risk | Not named. Related fixes: 0.10.0 stops the client treating a missing fragment as a missing branch, and 0.11.0 says a retried push no longer fails with a missing fragment. **Plausibly fixed; must verify** | Unconditional `QEXPECT_FAIL` in `knownRemotePromotesAndSurvivesFailures`; alpha disclosure in `PromotionDialog.qml` |
| No public way to attach a remote to an existing local repository | [EpicGames/lore#192](https://github.com/EpicGames/lore/issues/192) (open feature request) | Yes. `repository config set remote_url` is refused | Nothing added. **Still a gap** | Atomic `.lore/config.toml` edit; `noRemoteAttachesThroughDocumentedConfig` |
| Branch can advance ahead of its state | Not filed | No. 0.9.0 fixed the ordering | 0.14.0 also fixes concurrent branch-metadata edits undoing each other | None needed |

"What release notes say" reflects only the notes. Treat an issue as resolved
only after the matching test passes against the new pin, as described in the
upgrade plan.

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

Full reproduction steps are in the upstream issues linked above.

## Maintenance rules

- Every allowance for a LORE defect in a test must be gated on the exact pinned
  version and cite an entry on this page. When the pin changes, an allowance
  that is no longer needed must turn into a hard failure or an unexpected pass,
  not stay silently tolerated.
- When an upstream issue closes, or a release claims a fix, record the version
  here. Remove the workaround only after pimio's own test proves the fix.
