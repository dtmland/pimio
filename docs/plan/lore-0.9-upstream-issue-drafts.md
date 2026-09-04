# Draft LORE upstream reports after 0.9.0 verification

LORE 0.9.0 was released on 2026-08-31. Its
[release notes](https://github.com/EpicGames/lore/releases/tag/v0.9.0) describe
a new asynchronous I/O engine and multiple storage correctness fixes, but do not
explicitly say that the three interrupted-commit failures pimio observed with
0.8.5 were fixed.

These are **drafts, not claims about 0.9.0**. Run the Increment 7.8a fault suite
against unmodified 0.9.0 first. File only a draft whose behavior still
reproduces, replacing placeholders with the smallest reproducer, logs, platform,
and exact build information. If a test no longer reproduces, ask whether the
relevant change can be identified so pimio can cite it in Decision 0001.

## Draft 1 — Interrupted local-store write can leave an unreadable pending marker

**Title:** Interrupted local-store write can leave zero-length `level.pending`
that blocks repository open

**Body:**

> We are fault-testing LORE as an embedded local/offline store. With LORE
> `<exact 0.9.0 build>`, terminating the process during a commit can leave a
> zero-byte `.lore/immutable/index/<group>/level.pending`. Every subsequent
> repository open fails with `failed to fill whole buffer`.
>
> On 0.8.5 we observed this in roughly 5% of about 140 kill-during-commit trials.
> Removing only the empty marker after proving no writer is active restored
> readable history and allowed later commits, but an application should not
> need to interpret LORE's private store files.
>
> Reproducer: `<attach minimized loop and exact kill point>`
>
> Expected: opening the repository recovers or reports a supported repair action
> without requiring private-file deletion.
>
> Actual: `<0.9.0 logs and resulting store diagnostics>`
>
> Is this intended to be covered by the 0.9.0 local-store recovery work? If so,
> which change defines the supported recovery contract?

## Draft 2 — Successful commit is lost unless followed by repository flush

**Title:** Clarify or enforce durability of a successful local revision commit

**Body:**

> With LORE `<exact 0.9.0 build>` in local/offline sync-data mode, our fault test
> receives successful revision-commit completion, then terminates the process
> before issuing `repository flush`. After reopening, the reported revision is
> sometimes absent.
>
> On 0.8.5 this reproduced in about 20% of trials. Issuing `repository flush`
> before acknowledging the save eliminated the observed loss. We need to know
> whether successful commit completion is intended to be durable, or whether
> every embedding application must treat a following flush as part of commit.
>
> Reproducer: `<attach minimized C API or CLI reproducer>`
>
> Expected: either the reported revision survives process termination, or the
> public API documentation explicitly states that commit is not durable until a
> successful flush.
>
> Actual: `<0.9.0 revision id, history before/after, and logs>`

## Draft 3 — Branch can advance to a revision whose state object is absent

**Title:** Interrupted commit can leave branch head referencing a non-durable
state and reject future commits

**Body:**

> During repeated kill-during-commit testing with LORE
> `<exact 0.9.0 build>`, a branch head advanced to a revision whose state object
> was not present after restart. Existing history and committed files remained
> readable, but every later commit failed with `Branch has been advanced by
> another instance, sync and re-stage to commit`.
>
> On 0.8.5 this occurred once in about 140 trials. `repository instance prune`,
> `repository gc`, and `revision sync` to the last readable revision did not
> restore writability.
>
> Reproducer: `<attach minimized loop and damaged repository diagnostics>`
>
> Expected: the branch pointer becomes durable only after the state it names, or
> opening the repository rolls back/repairs an incomplete advance through a
> documented operation.
>
> Actual: `<0.9.0 logs, branch/revision diagnostics, and repair attempts>`
>
> Is there a supported recovery operation that preserves the readable history?

