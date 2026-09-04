# 0006 — Local-first LORE topology and recovery ownership

Status: **accepted local-first direction; promotion gate completed and blocked on LORE 0.9.0.**

## Decision

pimio 1.0 uses `liblore` in-process as a local, offline repository. It does not
run or package `loreserver`. A repository may be created locally without a
server, and the desktop application must remain useful without a network.

The headless **pimio Server** remains a later product milestone. It hosts pimio
services and one or more LORE repositories; it is not a prerequisite for a
standalone Library. Before 1.0 ships, a feasibility gate must nevertheless prove
that a locally created Library can later be promoted to a LORE server without
changing its library identity or losing history.

LORE documents offline work when the remote URL is already known, but no public
0.9.0 command was found for attaching a repository created with no remote at all:
`repository config` reads configuration, and recreating an existing repository
is not a migration operation. The gate must resolve this distinction rather than
assuming that editing `remote_url` is sufficient.

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
identity, records, and bytes. LORE 0.9.0 nevertheless fails the required
contract in four places: mismatched registration is not rejected, an
interrupted initial push is not retryable after remote branch creation, a fresh
clone has no offline history, and no public operation attaches a remote to a
no-remote origin. Server promotion therefore remains unavailable for v1.

LORE 0.9.0's release notes describe a new I/O engine and several storage fixes,
but do not explicitly identify the three 0.8.5 failures recorded in
[decision 0001](0001-lore-durable-store.md). The upgrade gate must reproduce the
tests rather than infer a fix from adjacent release-note language. Draft
upstream reports are in
[the LORE 0.9 issue drafts](../plan/lore-0.9-upstream-issue-drafts.md).

## Consequences

- The v1 executable and release archive contain `liblore`, not `loreserver`.
- v2 still introduces the headless pimio Server and ordinary remote operation.
- The v1 promotion gate validates storage portability, not a preliminary pimio
  Server product or remote user interface.
- The managed-originals decision is reopened because its principal no-go reason
  was the whole-store snapshot. Checkout/store duplication still needs an
  explicit product decision.
- Decisions 0001 and 0005 remain as brief historical evidence, with their
  superseded conclusions clearly marked.
