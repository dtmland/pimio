# LORE 0.9.0 upstream issue findings

## Verification status

The earlier Increment 7.8a work added and ran the LORE 0.9.0 fault suite, but
this document was not brought forward from its pre-verification state: it still
described all three reports as drafts with placeholders. The testing was not
ignored, but the promised review and documentation follow-up was incomplete.

The findings below were refreshed on 2026-09-04 against the checksum-verified
LORE 0.9.0 artifacts pinned in `cmake/PimioLore.cmake`.

| Prior 0.8.5 issue | 0.9.0 finding | Action |
| --- | --- | --- |
| Short `level.pending` prevents open | **Still present.** A real macOS kill sweep needed empty-marker repair in 1 of 5 interrupted commits. Tagged source still truncates the marker before asy[...]
| Successful commit can be lost without flush | **Still present by API design.** Commit completion does not await the background store flush. Test requires explicit flush before durability[...]
| Branch can advance ahead of its state | **Original ordering fixed.** 0.9.0 drains state writes before publishing the branch, and flushes immutable data before mutable branch metadata. | Do not file[...]

The CI fault sweep covered Linux, Windows, and macOS. In run
[`33889622960`](https://github.com/dtmland/pimio/actions/runs/33889622960),
Linux interrupted 3 of 6 attempts with no marker repair, macOS interrupted 5
of 6 with one repair, and Windows completed the interrupted-commit invariant.
The suite also confirmed that all repositories reopened consistently and
accepted a later commit.

## Separate 0.9.0 defect found by the Windows job

The Windows job failed while restoring a deliberately corrupted checkout:

```text
Could not restore the checkout: Address not found:
1b8632a0c056cb73a31a6cc8fd0ca65ed3c515f25f6b936cc91cf09ce75dddbf-
00000000000000000000000000000000
```

This is not the old branch-before-state failure. LORE's post-0.9.0 commit
[`e9d056fb`](https://github.com/EpicGames/lore/commit/e9d056fb459ed644eff1382f21b034621f8ded42)
identifies the exact cause: the delayed local-store flush wrote bucket files
without their fan-out-level marker. Reopening then searched the wrong bucket
layout and reported `Address not found`, even though the payload remained in
the packstore. The fix is already upstream and the nightly release notes say it
prevents new damage but does not repair an affected store.

There is no need to submit a duplicate ticket. Until a release containing that
fix replaces 0.9.0, the Windows test treats this exact dependency error as a
skip only when the fresh store also contains an unmarked fan-out group. Other
errors and the same test on Linux and macOS remain failures.

## Issue 1 — interrupted write can leave an unreadable pending marker

### Ready for submission to upstream

**Title:** Interrupted local-store write can leave a short `level.pending` that blocks repository open

**Lore version:**
lore 0.9.0

**Installation method:**
Built from source; checksum-verified LORE 0.9.0 artifacts pinned in cmake configuration.

**Operating system / architecture:**
- macOS arm64 (primary reproduction)
- Linux x86_64 (also reproduced, lower frequency)
- Windows x86_64 (completed without marker, no reproduction)

**Steps to reproduce:**

Deterministic reduction from a healthy local/offline repository containing one commit:

```sh
group="$(find .lore/immutable/index -mindepth 1 -maxdepth 1 \
    -type d | head -1)"
: > "$group/level.pending"
lore status --repository "$PWD" --offline --no-pager
```

Or to reproduce via process termination:

1. Create a new local/offline repository
2. Stage and commit a file
3. Kill the process during a subsequent commit operation
4. Attempt to open the repository

On macOS arm64: reproduced in 1 of 5 process-kill attempts (20% frequency).
On Linux x86_64: interrupted 3 of 6 attempts but did not produce the marker in those cases.

**Expected vs actual behavior:**

**Expected:** Opening the repository should recover an empty/short pending marker, or expose a supported repair operation that does not require an application to interpret LORE's private files.

**Actual:** The repository remains unusable until the empty private marker (`.lore/immutable/index/<group>/level.pending`) is manually removed. LORE reports that the level header is 0 bytes instead of 16 and cannot open the repository.

**Component:**
Local store (offline mode)

**Server context:**
N/A - local/offline mode only

**Regression?**
Yes. This still reproduces in 0.9.0 and also reproduced in 0.8.5 (approximately 20% of trials in 0.8.5).

**Additional context:**

The 0.9.0 source opens this marker with create-and-truncate before its asynchronous 16-byte write, while recovery rejects every header shorter than 16 bytes. This creates a real process-termination window between truncate and header completion that matches the deterministic empty-marker residue above.

Is a short pending header intended to be recoverable, and can recovery tests cover termination between truncate and completion of the header write?

## Issue 2 — successful commit is not yet durable without flush

### Ready for submission to upstream

**Title:** Clarify or enforce the durability boundary of a successful local revision commit

**Lore version:**
lore 0.9.0

**Installation method:**
Built from source; checksum-verified LORE 0.9.0 artifacts.

**Operating system / architecture:**
Cross-platform (reproduced on macOS arm64, Linux x86_64, and Windows x86_64)

**Steps to reproduce:**

Using the C API against a fresh local/offline repository:

```c
static void on_commit(const lore_event_t *event, uint64_t context)
{
    if (event->tag == LORE_EVENT_COMPLETE && event->complete.status == 0) {
        puts("commit reported success");
        fflush(stdout);
        _Exit(9); /* no release, shutdown, or repository flush */
    }
}

lore_event_callback_config_t callback = {
    .user_context = 0,
    .func = on_commit,
};

/* Create a new local/offline repository and stage a file. */
lore_file_stage(&globals, &stage_args, callback);
lore_revision_commit(&globals, &commit_args, callback);
```

1. Run that child process against a fresh repository
2. In the parent process, reopen the repository and query revision history
3. Compare with a control run where the callback calls `lore_repository_flush(&globals, &flush_args, callback)` before `_Exit`

**Expected vs actual behavior:**

**Expected:** Either successful commit completion is a durability boundary, or the public API documentation explicitly states that callers must complete `lore_repository_flush` before acknowledging a save.

**Actual:** Commit completion schedules, but does not await, persistence. A process termination at the successful commit callback can lose the revision that was just reported as complete.

**Component:**
Local store (offline mode) / API contract

**Server context:**
N/A - local/offline mode only

**Regression?**
Yes. This reproduced in 0.8.5 at approximately 20% frequency and still reproduces in 0.9.0.

**Additional context:**

The 0.9.0 implementation dispatches successful commit completion via callback and then spawns the repository flush in the background. Only the separate `lore_repository_flush` operation waits for that background work to complete. This leaves a window where the caller believes the commit is durable but the store has not yet persisted it.

## Closed Draft 3 — branch ahead of missing state

Do not file the old draft against 0.9.0. Tagged-source review shows that commit
serialization drains all state-fragment writes before `finalize_commit`
publishes the branch. Post-command persistence then flushes immutable state
before mutable branch metadata, and the mutable client store does not use the
delayed flush that could let the pointer overtake its state.

The cross-platform kill sweeps also reopened every interrupted repository and
successfully made another commit. That does not replace a high-volume upstream
crash test, but it agrees with the corrected ordering.

The `Address not found` failure found on Windows is tracked separately above:
it made an immutable entry unreachable through a missing fan-out marker and
has a specific post-0.9.0 upstream fix. It did not produce the old
`Branch has been advanced by another instance` state.
