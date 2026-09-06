# 0006 — Local-first LORE topology and recovery ownership

Status: **accepted; user-facing promotion enabled with the interrupted-push
defect accepted during alpha.**

## Decision

pimio 1.0 uses `liblore` in-process as a local, offline repository. It does not
run or package `loreserver`. A repository may be created locally without a
server, and the desktop application must remain useful without a network.

The headless **pimio Server** remains a later product milestone. It hosts pimio
services and one or more LORE repositories; it is not a prerequisite for a
standalone Library. Before 1.0 ships, a feasibility gate must nevertheless prove
that a locally created Library can later be promoted to a LORE server without
changing its library identity or losing history.

LORE documents offline work when the remote URL is already known. It has no
public 0.9.0 attach command, but its configuration reference explicitly permits
editing `.lore/config.toml`. The gate therefore verifies an atomic `remote_url`
edit rather than treating the file format as private or assuming that an
untested edit is sufficient.

pimio will also remove its whole-`.lore` pre-commit snapshot and rollback marker.
Storage-engine atomicity and recovery belong in LORE. pimio will retain the
application-level safeguards that do not copy the repository: single-writer
coordination, explicit flush before acknowledging a checkpoint, preservation of
staged input until success, checkout restoration, and visible repair errors.

## Why

The earlier v1 plan assumed that a LORE server was required for a repository to
exist. The implemented adapter and LORE's offline creation support show that it
is not. Running a server in every desktop process would add deployment,
authentication, lifecycle, and storage responsibilities without providing a v1
user benefit.

The rollback snapshot was introduced after LORE 0.8.5 fault testing found rare
interrupted-write failures. It copies the complete `.lore` store before every
pimio commit. That makes commit cost and temporary free-space requirements grow
with the entire Library, which is unsuitable for managed photo and video
content. The observations remain useful, but the workaround is not part of the
target architecture.

## Required gates

The implementation plan separates three pieces of work:

1. Upgrade all build contexts from LORE 0.8.5 to 0.9.0, adapt to its C API, and
   rerun the adapter, compatibility, migration, and fault suites on every
   supported platform.
2. Remove the whole-store rollback implementation while retaining the narrower
   safeguards above. A failure that LORE cannot repair must preserve user input,
   stop further writes, and produce a visible repair path.
3. Prove local creation, later server registration or attachment, initial push
   of the complete revision graph and fragments, fresh clone, identity
   preservation, and interrupted/rejected-push recovery. Compare a repository
   created with a known-but-unreachable remote to one created with no remote.

Increment 7.8b completed the third gate. A known-remote origin can make the
basic create-with-id, push, and clone round trip, preserving current pimio
identity, records, bytes, and revision history. Earlier revision state and
metadata are fetched lazily by an online history query and remain available to
pimio offline afterward; neither clone nor history caches every historical file
payload.

Two of the three integration gaps have bounded application-level handling.
Before promotion, pimio can query the registered repository and reject a remote
whose ID differs from the local ID. For a no-remote origin, LORE's configuration
reference explicitly permits editing `.lore/config.toml`; an atomic
`remote_url` update followed by identity preflight, push, and clone succeeds in
the retained gate. LORE should still validate identity on push and provide a
public attach operation, so both gaps have upstream issue drafts.

The remaining LORE 0.9.0 defect is interrupted initial-push recovery. If the
client dies after remote branch creation, retry fails because `main` already
exists. `--force`, recreating the server registration, and pushing a recovery
branch did not provide a complete, byte-readable clone. The local origin remains
writable, but promotion cannot safely continue without server-side repair or an
upstream fix.

The local-first architecture and user-facing promotion are therefore accepted:
the ordinary path works, identity is guarded, and attachment has a
documented-format fallback. During the alpha pre-release period, the remaining
interrupted-push defect is an explicitly accepted upstream risk rather than a
shipping gate. The UI warns that an interrupted first push may require
server-side repair. The contract test retains the expected failure so an
upstream fix becomes an unexpected pass that must be reviewed.

## History hydration and retention

LORE history has three independently relevant layers:

1. revision state and metadata, including IDs, parent links, timestamps, and
   messages;
2. each revision's historical tree and file metadata; and
3. the historical file payload fragments themselves.

A normal clone obtains the selected revision and materializes its current view.
An online `history` query walks prior revisions and caches enough state and
metadata for the same history listing to work later offline. It does not walk
every historical tree or fetch every old file payload. `--cache` retains
fragments that an operation actually requests; it does not turn clone or
history into a complete mirror.

LORE exposes bounded repository history and `file history <path> [LENGTH]`.
That makes future policies such as “discover the latest N revisions for selected
files” plausible. Offline availability of those versions would still require
pimio to fetch each selected revision/path explicitly, size the local immutable
store so those fragments are not evicted, and verify the result offline.
Renames, deletions, branches, views, and storage limits make this a separate
retention feature, not a clone flag or a consequence of history listing.

LORE 0.9.0's release notes describe a new I/O engine and several storage fixes,
but do not explicitly identify the three 0.8.5 failures recorded in
[decision 0001](0001-lore-durable-store.md). The upgrade gate must reproduce the
tests rather than infer a fix from adjacent release-note language. Draft
upstream reports are in
[the LORE 0.9 issue drafts](../plan/lore-0.9-upstream-issue-drafts.md).

## Consequences

- The v1 executable and release archive contain `liblore`, not `loreserver`.
- v2 still introduces the headless pimio Server and ordinary remote operation.
- The desktop UI can promote an idle local Library to a user-supplied LORE URL;
  it does not package a server or implement background synchronization.
- Increment 7.8c retests managed originals without the whole-store snapshot and
  keeps referenced originals for v1. Metadata commits are corpus-independent,
  but checkout/store and complete-backup duplication remain.
- Decision 0001 retains its superseded recovery evidence. Decision 0005 records
  the final referenced-originals choice after the Increment 7.8c retest.
