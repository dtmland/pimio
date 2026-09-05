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
fix replaces 0.9.0, tests tolerate this exact dependency error only when the
fresh store also contains an unmarked fan-out group. The corrupt-checkout test
has observed it on Windows, where it skips, and the server-promotion test has
observed it on Linux after a deliberately invalid raw push, where it records the
failure and continues the independent valid-promotion checks. Other errors
remain failures.

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

### Temporary test policy and release follow-up

`lore.faults` continues to run its complete process-kill sweep on every CI
platform. While pimio pins LORE 0.9.0, repository reopen failures immediately
following the deliberate mid-commit process kill are logged with their delay and
diagnostic, then reported as a skipped test after the sweep completes instead of
failing CI. Observed diagnostics include the zero-byte level header documented
above and `Could not restore the checkout: Not found`. The allowance is limited
to LORE 0.9.0 and this fault-injection boundary: setup failures and every
consistency or subsequent-commit failure after a successful reopen remain
blocking.

When a new LORE release is available:

1. Review its release notes and the upstream disposition of this issue.
2. Update the pin consistently across all build contexts and confirm the drift
   checks pass.
3. Run `lore.faults` repeatedly on Linux, Windows, and macOS, retaining the
   per-delay outcome logs.
4. Remove the 0.9.0 allowance. Adjust recovery or tests only when the new
   release's documented behavior and repeated fault results justify it.
5. Update this finding with the tested version, CI runs, and conclusion.

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

## Issue 3 — push does not validate the registered repository identity

### Ready for submission to upstream

**Title:** Push accepts a repository ID that differs from the ID registered for the remote name

**Lore version:**
lore 0.9.0

**Installation method:**
Checksum-verified LORE 0.9.0 CLI and loreserver release artifacts.

**Operating system / architecture:**
Linux x86_64

**Steps to reproduce:**

1. Create an offline repository whose `.lore/id` is `A` and configure its
   `remote_url` as `lore://127.0.0.1:<port>/library`.
2. Create the server repository named `library` with explicit repository ID
   `B`, where `A != B`.
3. Commit content in the offline repository.
4. Run `lore push` from the offline repository.

The automated reproduction is
`TestLoreServerPromotion::knownRemotePromotesAndSurvivesFailures` in
`tests/lore/tst_lore_server_promotion.cpp`.

**Expected vs actual behavior:**

**Expected:** Push rejects the request before transferring or advancing data
because the repository ID registered for `library` differs from the local
repository ID.

**Actual:** Push exits successfully and reports that it pushed the revision.

**Component:**
Client/server repository identity validation

**Server context:**
Unauthenticated loopback `loreserver` with isolated local immutable and mutable
stores.

**Regression?**
Unknown.

**Additional context:**

`repository create` validates name-to-ID consistency, and `repository info`
returns the registered ID, but the push path does not enforce the same
invariant. A client can preflight with `repository info`, but that leaves a
time-of-check/time-of-use window. The server should enforce identity at the
write boundary.

### Temporary test policy and release follow-up

The promotion gate retains the raw push as an expected failure and separately
proves that pimio can detect the mismatch through `repository info` before
invoking push. Remove the client-side requirement only after a pinned LORE
release rejects the raw push itself.

## Issue 4 — interrupted initial push cannot be retried

### Ready for submission to upstream

**Title:** Retrying an interrupted initial push fails because the remote branch already exists

**Lore version:**
lore 0.9.0

**Installation method:**
Checksum-verified LORE 0.9.0 CLI and loreserver release artifacts.

**Operating system / architecture:**
Linux x86_64

**Steps to reproduce:**

1. Create and populate an offline repository, including a sufficiently large
   payload to keep the initial transfer active.
2. Register the corresponding server repository with the same repository ID.
3. Start `lore push` with one connection.
4. Terminate the client after it reports `Pushing` and before it completes.
5. Confirm the offline origin still opens and accepts another commit.
6. Run `lore push` again.

The automated reproduction is
`TestLoreServerPromotion::knownRemotePromotesAndSurvivesFailures` in
`tests/lore/tst_lore_server_promotion.cpp`.

**Expected vs actual behavior:**

**Expected:** Repeating the push resumes or safely restarts the transfer and
advances the existing remote branch.

**Actual:** Retry fails with `Branch main already exists, use switch instead`.
The partially transferred repository cannot be completed through another
ordinary push.

**Component:**
Client/server initial push and branch recovery

**Server context:**
Unauthenticated loopback `loreserver` with isolated local immutable and mutable
stores.

**Regression?**
Unknown.

**Additional context:**

The failure is returned while `branch::push` attempts to recreate `main` after
the server reports missing state. `--force` does not recover it. Creating and
pushing a new branch can publish the revision state but may still leave
previously transferred payload addresses absent, so that is not a safe
workaround. Server-side removal of the partial repository also requires
authorization and is not a general client recovery path.

### Temporary test policy and release follow-up

Keep the retry assertion as an expected failure for exactly LORE 0.9.0. Initial
promotion must remain unavailable as a user-facing operation until a repeated
push, or a documented non-destructive recovery sequence, passes the complete
clone-and-byte verification.

## Feature request 5 — attach an existing local repository to a remote

### Ready for submission to upstream

**Title:** Add a public command or API to attach a remote URL to an existing local repository

**Lore version:**
lore 0.9.0

**Installation method:**
Checksum-verified LORE 0.9.0 CLI and loreserver release artifacts.

**Operating system / architecture:**
Cross-platform API request; workaround verified on Linux x86_64.

**Use case:**

An application creates and uses a repository entirely offline, then later lets
the user promote that same repository to a server without changing its
repository identity or replaying its history.

**Current behavior:**

`repository config` exposes `get` but no setter or attach operation.
`repository create` and `clone` initialize new local repositories rather than
attaching an existing one. The configuration reference permits editing
`.lore/config.toml` by hand, and setting `remote_url` there works, but every
embedding application must implement that mutation itself.

**Requested behavior:**

Provide a supported command and C API operation that:

1. accepts a remote URL for an existing local repository;
2. optionally validates or registers the remote name with the local repository
   ID;
3. atomically persists `remote_url`;
4. rejects mismatched remote identities; and
5. leaves the previous configuration unchanged on any failure.

**Component:**
Repository configuration / offline-to-server workflow

**Server context:**
Applies before the initial push to a Lore Server.

**Additional context:**

pimio's temporary workaround reads the existing configuration, changes only
`remote_url`, and atomically replaces `config.toml` through a same-directory
temporary file. Its contract test then performs identity preflight, push, clone,
and content verification. A public operation would remove duplicated TOML
handling and allow Lore to evolve the configuration schema safely.
