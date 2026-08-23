# 0001 — LORE as pimio's durable store

Status: **accepted with conditions.** Recorded at the end of Increment 2
(units 2a–2c). This is the go/no-go the increment exists to produce.

Decision owner: repository maintainer. This record states what was measured,
what was decided, and what must still be true before v1 ships.

## Decision

Adopt LORE 0.8.5 as the authoritative durable store behind
`pimio::core::DurableStore`, subject to the four conditions in
[Conditions](#conditions). Do not promote SQLite to authoritative; it remains a
disposable projection, as Increment 3 assumes.

## Context

`docs/plan/pimio-v1-implementation.md` makes the durable store the ground truth for every
media record, and makes the SQLite database a cache that must be deletable
without loss. That only works if the durable store is genuinely durable, is
readable by something other than pimio, and survives the failures a desktop
application actually meets: the user closing the laptop lid, the process being
killed, a full disk, a read-only volume, and a second copy of the app starting.

LORE is a content-addressed versioned store from Epic Games, published as
MIT-licensed artifacts with a C API. Increment 2 exists to decide whether it can
carry that weight, because the alternative — inventing a versioned store — is a
large amount of work that is easy to get wrong.

## What was built

`src/lore/` implements `LoreDurableStore`, a replaceable adapter mapping
`stage`, `commit`, `discardStaged`, `load`, `listIds`, `history`, and
`stateToken` onto local, offline LORE operations. `lore.h` is private to that
directory and the library is resolved by name at runtime through `QLibrary`, so
a missing or unloadable artifact produces a visible unavailable state rather
than a launch failure.

Store layout:

```
<store>/repository/                 LORE checkout and its .lore directory
<store>/repository/records/<xx>/    committed records, one JSON file each
<store>/staging/                    staged records, mirroring the same layout
<store>/.pimio-writer.lock          single-writer lock
```

Staging sits deliberately **outside** the checkout. `load()` and `listIds()`
read the checkout only, so they can structurally never report staged work as
committed — the invariant is a property of the layout rather than of careful
coding.

## Design decisions recorded

### 1. One file per media record, batched into one commit

Each record is its own JSON file under a single-level shard derived from its
id. A `commit()` copies everything currently staged into the checkout and
commits it as one revision.

The alternative — packing many records into one file — would make commits
cheaper but would make a single record's history unreadable, would turn every
edit into a rewrite of a large file, and would make concurrent-edit conflicts
whole-file conflicts. Measured cost (below) does not justify that.

The shard exists because LORE rejects a tree containing two names that differ
only by case, and because flat directories with hundreds of thousands of
entries are hostile to every tool a user might point at them. Record ids are
opaque, so any id that is not already lowercase `[a-z0-9._-]` is hex-encoded
into a `x-<hex>.json` name.

### 2. pimio metadata rides as file content, not as LORE metadata

The record is serialized with pimio's own versioned JSON, the same format the
in-memory store uses, preserving unknown fields for forward compatibility.
LORE's file and revision metadata are not used to carry pimio data.

This keeps the schema owned by `pimio::core`, keeps records readable with a
text editor and diffable by the `lore` CLI, and keeps the LORE dependency
narrow enough that a version bump or a replacement store is a contained change.
LORE revision metadata is read, not written: `history()` takes the checkpoint
message and timestamp from it.

## Process and API boundary

- LORE runs in-process as a dynamically loaded C library. Every call is
  `int32_t f(globals, args, callbacks)` with results delivered as events on
  LORE worker threads, so the adapter collects them under a mutex.
- Globals are fixed to `offline`, `local`, and `sync_data`. Nothing contacts a
  server; a repository URL is required syntactically at creation and is never
  resolved.
- `sync_data` is kept even though it does not prevent the defects below: it is
  what makes committed content durable against host power loss, which is a
  different failure from a process kill. It governs how LORE writes, not when
  it decides to write, so `commit()` also flushes explicitly.
- LORE resolves relative paths against the process working directory, so the
  adapter always passes absolute native paths.
- The `lore` CLI is test-only. It is used to prove a pimio repository is
  readable and writable by the reference implementation, which is the property
  that makes the store genuinely non-proprietary.

## Locking

pimio takes an exclusive `QLockFile` on the store before touching LORE, and a
second writer is refused with a conflict error naming the holding process.

This is not belt-and-braces. LORE 0.8.5 does not serialise concurrent
committers safely: two processes committing into one repository were observed
leaving its local store in a state that needed repair, with no process killed.
Serialising above LORE removes the failure entirely, and single-writer is the
correct product behaviour anyway — one library, one application instance.

## Failure behaviour observed

Tested by `lore.faults`, on every CI platform.

| Failure | Behaviour |
| --- | --- |
| Process killed after records reach the checkout, before commit | Checkout is restored on next open; staged work is intact and re-committable |
| Process killed during commit | Commit either landed whole or not at all; never partially |
| Process killed immediately after a successful commit | The reported revision is still there: `commit()` flushes before it returns |
| Checkout deleted entirely | Rebuilt from the committed revision, no loss |
| Record file corrupted in the checkout | Detected, and repaired from the committed revision |
| Checkout made read-only | Commit fails visibly, staged work is preserved |
| Second process writing concurrently | Refused by the writer lock before reaching LORE |

Three rules follow. The first is on the write path, the other two are
implemented in `open()`:

1. **A commit is not finished until it is flushed.** See the defect below.
   Before changing the checkout or LORE's staged index, `commit()` snapshots the
   clean committed repository and writes a rollback marker. It calls
   `repository flush` before it reports a checkpoint, and only then retires the
   rollback marker and clears the staging area.
2. **Always restore the checkout.** An interrupted commit leaves untracked
   files, and LORE's dirty check does not see untracked files, so a cheap
   status cannot be trusted to decide whether recovery is needed. The full
   restore is paid once per open.
3. **Clear interrupted-write residue.** See the defect below.

Disk-full is not simulated: filling a real volume portably is not something a
test suite should do to a contributor's machine. The write path uses `QSaveFile`
so a truncated write cannot replace a good record, and the out-of-space error
path is exercised through the permission-failure test, which takes the same
branch. A genuine full-volume run stays a documented manual test.

## Known defects in LORE 0.8.5

### Interrupted writes can leave an empty pending marker

A process killed inside LORE's local store can leave a zero-length
`level.pending` file in `.lore/immutable/index/<group>/`. Every later open then
fails with `failed to fill whole buffer` while trying to read a fixed-size
record back out of it.

Observed in roughly 5% of ~140 kill-during-commit trials.

An empty marker records no transition, so removing it is equivalent to the
transition never having started. Verified on four independently damaged
repositories: after removal, history, all committed records, and further
commits were intact. `open()` performs this repair itself, but only while
holding the writer lock — that lock is what proves a marker is leftover rather
than a write in flight — and reports it through
`repairedInterruptedWriteOnOpen()` so a silent recovery never looks like a
clean start.

### A committed revision is not durable until the repository is flushed

`revision commit` reports a revision as committed before LORE has written it
out. A process that died between that report and `repository flush` — which
pimio originally issued only from `close()` — lost the revision entirely in
about 20% of trials, while the record files it had already copied into the
checkout stayed on disk. The application had told the user the save was safe,
the staging area had been cleared, and the work was gone.

The same window explains an intermittent `lore.faults` failure on Linux and
macOS. A kill shortly after `commit()` returned left LORE's revision log and
its index partially written and disagreeing with each other, and the two
outcomes seen in CI are the two directions of that disagreement:

* `history()` reported two revisions while `listIds()` reported one record —
  the revision log had the commit, the index did not, so the checkout restore
  purged the new records as untracked; and
* `history()` reported one revision while `listIds()` reported 26 records — the
  index had the files, the revision log did not.

`commit()` now flushes before it returns, so the checkpoint it hands back names
a revision that is on disk, and the marker-and-backup rollback is retired only
after that flush. Everything the commit touches — the LORE repository, the
checkout, and the staging area — is therefore either fully before or fully
after the commit at every instant a process can die.
`killedProcessAfterCommitKeepsTheRevisionItReported` is the regression test; it
failed on every attempt before the flush was added.

The ordering of the last two steps was wrong for the same reason. The staging
area used to be cleared before the rollback marker was removed, so a kill in
between rolled a landed commit back while its staged inputs were already partly
deleted — a batch that was neither applied nor recoverable. Retiring the
rollback first makes the worst case a repeat of work that is already committed,
which is harmless.

### A kill can advance the branch past a non-durable revision

Once in the same ~140 trials, a kill left the branch head pointing at a
revision whose state object was never written. The repository still reads:
history and every committed record are available. It refuses new commits with
`Branch has been advanced by another instance, sync and re-stage to commit`,
and no documented verb recovers it — `repository instance prune`,
`repository gc`, and `revision sync` to the last good revision all fail.

This is an ordering defect upstream: the branch pointer becomes visible before
the state it names is durable.

Blast radius is bounded — no committed data is lost, and it needs a kill inside
a sub-millisecond window — but a library that can permanently refuse new saves
is not acceptable for a shipping product. Hence condition 3.

## Measurements

2000 records, one JSON file each, on the Linux CI-class runner:

| Operation | Time |
| --- | --- |
| Stage 2000 records | ~0.9 s |
| First commit of 2000 records | ~1.2 s |
| Incremental commit of one further record | ~0.7 s |
| `listIds()` | ~80 ms |
| `stateToken()` | 7–40 ms |
| Repository on disk | ~3.3 MiB |

At 200 records a batched commit took ~160 ms against ~60 ms for a single
record, so commit cost is dominated by a per-commit constant rather than by the
number of records in it. Batching is therefore worth keeping, and per-edit
commits are affordable at interactive scale.

The number that matters for Increment 4 is the ~0.7 s incremental commit: a
scan that commits per file would be unusable at library scale. Ingest must
batch, and the job queue in Increment 3b has to be designed with that in mind.

## Upgrade and packaging

- The version is pinned in `cmake/PimioLore.cmake` with a SHA-256 per artifact.
  Nothing is fetched without a checksum match, and the extracted directory
  carries a stamp so a partial extraction is re-done rather than trusted.
- The cache is keyed by version, so a bump is a cache miss rather than a stale
  hit, locally and in CI alike.
- `PIMIO_WITH_LORE` keeps the dependency optional for contributors; CI sets
  `PIMIO_REQUIRE_LORE=ON` on all three jobs so a skipped durable-store test can
  never pass for green.
- Redistribution obligations are in `docs/dependency-bom.md`: the MIT licence
  and `THIRD-PARTY-NOTICES.txt` must ship with the application. This is
  Increment 12 work.
- No on-disk format owned by pimio changes when LORE changes, because records
  are pimio JSON. A LORE upgrade that changes its own repository format would
  need its own migration test before the pin moves.

## Conditions

1. **Single-writer enforcement stays.** Removing the lock re-exposes the
   concurrent-writer corruption. Any future multi-process feature must
   coordinate above LORE, not inside it.
2. **`commit()` stays a durability boundary.** It must flush before it reports
   a checkpoint and before it clears staged work. A caller that gets a
   `Checkpoint` is entitled to assume a power cut changes nothing.
3. **The interrupted-write repair stays visible.** It may be automatic, but it
   must be reported, and it must never widen beyond removing empty markers.
4. **The branch-advance defect must be closed before v1 ships.** Either
   upstream fixes it, or pimio gains a durable-store rebuild path that reads
   the surviving records and rewrites them into a fresh repository, losing
   revision history but no user data. This is a release blocker, not a
   nice-to-have, and it needs its own increment.
5. **The pin moves deliberately.** LORE is pre-1.0 and its C API is still
   gaining verbs. A version bump re-runs `lore.faults` on all three platforms
   before it lands.

## Consequences

- Increment 3 can proceed as written: SQLite is a disposable projection
  rebuilt from `stateToken()` and `restoreFromDurableState()`.
- macOS x86-64 is out of scope for v1, because upstream publishes no artifact.
  See `docs/supported-platforms.md`.
- The adapter is the only code that knows LORE exists. Replacing the durable
  store means writing another `DurableStore`, not touching the application.

## Addendum — the library-centric reorientation

The plans were later reoriented around the principle that one pimio Library
is one LORE repository with a stable identity (see
`docs/plan/pimio.md` and the reorientation section of
`docs/plan/pimio-v1-implementation.md`). This decision stands unchanged, and
two of its findings now carry more weight:

- The measurements above cover small JSON records only. Whether the
  repository can also hold *original media content* (a "managed" library) is
  an open question with its own feasibility gate, Increment 7.8.
- The single-writer condition shapes the multi-user future: v2/v3
  collaboration serializes writes in the pimio Server above LORE; nothing
  may rely on LORE tolerating concurrent committers.

## Alternatives considered

- **Git or libgit2.** Well understood and universally readable, but its
  performance on hundreds of thousands of small files is poor, and its
  packing behaviour is hard to bound for a library that only ever grows.
- **Build a versioned store.** Full control, but the failure modes catalogued
  above are exactly the ones that would have to be discovered and fixed from
  scratch, and none of them would be found by tests written by the same person
  who wrote the bug.
- **SQLite as the authoritative store.** Would collapse two components into
  one, but gives up per-record history and makes the database undeletable,
  which contradicts the v1 requirement that the cache be disposable.
