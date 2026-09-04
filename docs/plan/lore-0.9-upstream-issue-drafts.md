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
| Short `level.pending` prevents open | **Still present.** A real macOS kill sweep needed pimio's empty-marker repair in 1 of 5 interrupted commits. Tagged source still truncates the marker before asynchronously writing its 16-byte header, and rejects a short header on open. | File Draft 1. |
| Successful commit can be lost without flush | **Still present by API design.** Commit completion does not await the background store flush. pimio's test passes because its adapter explicitly flushes before returning success. | File Draft 2 to clarify or strengthen the public durability contract. |
| Branch can advance ahead of its state | **Original ordering fixed.** 0.9.0 drains state writes before publishing the branch, and flushes immutable data before mutable branch metadata. | Do not file Draft 3 for 0.9.0. A crash regression test upstream would still be useful. |

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
skip. Other errors and the same test on Linux and macOS remain failures.

## Draft 1 — interrupted write can leave an unreadable pending marker

**Title:** Interrupted local-store write can leave a short `level.pending` that
blocks repository open

**Body:**

> We are fault-testing LORE 0.9.0 as an embedded local/offline store. Killing a
> process during a commit can leave a zero-byte
> `.lore/immutable/index/<group>/level.pending`. Every subsequent repository
> operation fails while reading the short marker.
>
> This still reproduced with the checksum-verified 0.9.0 macOS arm64 artifact:
> five of six timed attempts interrupted a commit and one left the empty
> marker. The equivalent Linux sweep interrupted three of six attempts without
> producing the marker.
>
> A deterministic reduction is:
>
> ```sh
> # Run from a healthy local/offline repository containing one commit.
> group="$(find .lore/immutable/index -mindepth 1 -maxdepth 1 \
>     -type d | head -1)"
> : > "$group/level.pending"
> lore status --repository "$PWD" --offline --no-pager
> ```
>
> LORE reports that the level header is 0 bytes instead of 16 and cannot open
> the repository. The 0.9.0 source opens this marker with create-and-truncate
> before its asynchronous 16-byte write, while recovery rejects every header
> shorter than 16 bytes. This leaves a real process-termination window matching
> the deterministic residue above.
>
> Expected: opening the repository should recover an empty/short pending marker,
> or expose a supported repair operation that does not require an application
> to interpret LORE's private files.
>
> Actual: the repository remains unusable until the empty private marker is
> removed. pimio currently performs that narrow repair only after taking its
> exclusive writer lock and reports that recovery to the caller.
>
> Is a short pending header intended to be recoverable, and can recovery tests
> cover termination between truncate and completion of the header write?

The full process-kill implementation is in
`tests/lore/fault_helper_main.cpp` (`crash-during-commit`) and the observing
loop is in `tests/lore/tst_lore_faults_process.cpp`.

## Draft 2 — successful commit is not yet durable without flush

**Title:** Clarify or enforce the durability boundary of a successful local
revision commit

**Body:**

> With LORE 0.9.0 in local/offline mode, a successful revision-commit callback
> is delivered before the post-command store flush has completed. Terminating
> at that callback can therefore lose the revision that was just reported.
>
> On 0.8.5 this reproduced in about 20% of trials. The 0.9.0 implementation
> still dispatches successful commit completion and then spawns the repository
> flush in the background. Only the separate `lore_repository_flush` operation
> waits for that work.
>
> The essential C API reproducer is:
>
> ```c
> static void on_commit(const lore_event_t *event, uint64_t context)
> {
>     if (event->tag == LORE_EVENT_COMPLETE && event->complete.status == 0) {
>         puts("commit reported success");
>         fflush(stdout);
>         _Exit(9); /* no release, shutdown, or repository flush */
>     }
> }
>
> lore_event_callback_config_t callback = {
>     .user_context = 0,
>     .func = on_commit,
> };
>
> /* globals selects a newly created local/offline repository; args names one
>    staged file. Both structs are zero-initialized before these calls. */
> lore_file_stage(&globals, &stage_args, callback);
> lore_revision_commit(&globals, &commit_args, callback);
> ```
>
> Run that child against a fresh repository, reopen it in the parent, and query
> revision history. Repeat with the callback calling
> `lore_repository_flush(&globals, &flush_args, callback)` before `_Exit`.
> Compare whether the revision reported by the commit survives. The flush arm
> is the control and is also pimio's current workaround.
>
> Expected: either successful commit completion is a durability boundary, or
> the public API documentation explicitly says that callers must complete
> `lore_repository_flush` before acknowledging a save.
>
> Actual: commit completion schedules, but does not await, persistence.

pimio's process-death harness is in `tests/lore/fault_helper_main.cpp`
(`commit-then-die`). Its adapter deliberately calls `lore_repository_flush`
before returning the checkpoint, so `killedProcessAfterCommitKeepsTheRevisionItReported`
validates the workaround rather than bare LORE commit semantics.

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
