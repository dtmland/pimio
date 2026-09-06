# 0005 — Managed originals for v1

Status: **accepted.** Initially recorded at the end of Increment 7.8 and
re-evaluated through the production LORE 0.9.0 path in Increment 7.8c.

## Decision

pimio uses managed libraries for v1: original media is copied into the Library's
LORE repository and committed alongside its descriptor, metadata, organization,
edit recipes, and history. The source path is import provenance, not durable
Library storage.

The measured checkout plus immutable-store duplication is accepted. Removing
pimio's whole-store recovery copy makes ordinary metadata commits independent
of corpus size, and keeping originals in LORE satisfies the product requirement
that a Library is self-contained, portable, and promoted as one unit.

## Context

The library-centric design aims to make a Library portable and self-contained,
but the implementation before Increment 7.8c versioned JSON records and
referenced original media in place. Increment 2 measured LORE with small records only. Increment
7.8 therefore tested whether LORE 0.8.5 can carry compressed-media-sized binary
content before any managed ingest was designed.

The gate is `lore.binary_content`. It creates deterministic, effectively
incompressible binary content to model the storage characteristics of a
compressed video in a Library created by the production adapter, commits it
through the independently published LORE CLI, starts a fresh CLI process to
restore it, verifies its SHA-256, and commits the same content at a second path
to measure deduplication. It then commits a metadata-only change through
`LoreDurableStore`, copies and restores the complete store, and verifies the
library identity, metadata, and original bytes. Its default 8 MiB size keeps
normal CI economical. Set `PIMIO_LORE_BINARY_SPIKE_MIB=256` to reproduce the
multi-hundred-MB run.

## Measurements

The original 256 MiB run was performed on 2026-09-01 on the Linux x86-64 task
runner with LORE 0.8.5. Increment 7.8c repeats the same workload with pinned
LORE 0.9.0 and additionally records the production-adapter metadata commit and
whole-store backup/restore.

| Operation | Result |
| --- | ---: |
| Generate 256 MiB payload | 0.79 s |
| Stage first payload | 0.03 s |
| Commit first payload | 1.47 s |
| Restore deleted checkout file | 0.55 s |
| SHA-256 read-back | 0.19 s |
| Commit identical content at a second path | 0.87 s |
| Repository after first commit | 537,361,346 bytes |
| Repository after duplicate-path commit | 806,109,628 bytes |
| `.lore` after duplicate-path commit | 269,238,716 bytes |

The restored SHA-256 exactly matched the committed payload. LORE deduplicated
the identical content in its immutable store: the second path added another
256 MiB checkout file but did not add another full payload under `.lore`.

The 0.9.0 gate prints measurements for the payload size, checkout and
immutable-store size, metadata commit time and growth, and backup/restore size
and time on every platform. The 256 MiB run below was performed on 2026-09-06
on the Linux x86-64 task runner.

| Operation | LORE 0.9.0 result |
| --- | ---: |
| Stage first payload | 0.015 s |
| Commit first payload | 0.311 s |
| Restore deleted checkout file | 0.175 s |
| Repository after first payload | 537,324,909 bytes |
| Checkout after first payload | 268,435,694 bytes |
| `.lore` after first payload | 268,889,215 bytes |
| Commit identical content at a second path | 0.272 s |
| `.lore` growth for duplicate content | 315,911 bytes |
| Production-adapter metadata commit | 0.091 s |
| Repository growth for metadata commit | 5,220 bytes |
| Complete store after duplicate path and metadata | 806,081,496 bytes |
| Copy complete store for backup | 4.923 s |
| Restore complete store from backup | 4.746 s |

One unique payload rested at 2.0017 times its source size. The duplicate path
added a second full checkout copy but only 0.12% of the payload size to
`.lore`. The metadata commit added 5.1 KiB and left no rollback-copy artifact; together
with the rollback implementation's removal in Increment 7.8a, its space cost
does not scale with the 256 MiB corpus. The complete-copy timings are
environment-specific; their capacity requirement is not.

## Why managed originals are accepted for v1

Binary correctness, restart, restore, and LORE's content deduplication pass.
The removed rollback copy also means an ordinary pimio metadata commit no
longer copies the corpus. Managed storage has explicit costs:

1. A checked-out original also exists in LORE's immutable store. The first
   payload therefore requires about twice its source size at rest before small
   repository overhead. Identical content is shared in `.lore`, but each
   checkout path remains a complete copy.
2. A self-contained backup must copy both the checkout and immutable store.
   While source and backup coexist, the same original therefore consumes about
   four times its source size. Restore needs the same two-copy destination
   capacity. LORE's server adds another immutable copy without removing either
   local copy.
3. Low-space failure can occur while staging checkout content, writing immutable
   fragments, or making a backup. The adapter maps write failures to a visible
   error and preserves staged metadata, but LORE exposes no reservation that
   could guarantee a multi-gigabyte original will finish after ingest starts.
   Hosted cross-platform tests cannot deterministically exhaust a volume or
   interrupt each binary-write phase, so ingest must preflight capacity, retain
   the source until commit succeeds, and report partial work visibly.

The gate is intentionally about the complete pimio storage path, not whether
LORE can read one large file in isolation. The 0.9.0 result removes the
repository-sized cost from metadata commits. The remaining resting, backup,
restore, low-space, and hosted-storage costs are accepted in exchange for one
self-contained Library lifecycle.

## Backup, restore, and portability consequences

A complete v1 Library backup contains the LORE repository, including its
committed originals, identity, canonical records, and history. The Library
Manager must take the backup from a quiescent durable checkpoint, verify it
before reporting success, and restore the same library id and original bytes at
a new location. Source import folders are not additional backup dependencies.

Moving or copying the repository carries the managed originals with its
identity. Promotion pushes the committed current originals with canonical state
and history; as documented in decision 0006, older historical payloads remain
subject to LORE's lazy hydration behavior.

Repositories containing pre-managed records remain readable. Such records are
decoded as explicitly referenced and therefore migration-incomplete; a scan can
copy an available source into LORE and commit the managed location. Missing
sources are retained for later repair and are never silently marked managed.

## Alternatives considered

- **Managed originals in LORE for v1.** Accepted. The self-contained lifecycle
  outweighs the measured capacity amplification, and metadata commits no longer
  copy the repository.
- **Managed and referenced modes in v1.** Rejected because two storage modes
  would double ingest, lifecycle, backup, restore, and promotion behavior.
- **Referenced originals.** Rejected because a repository-only move, backup, or
  promotion would omit the media and require users to manage a second durable
  storage topology.

## Revisit criteria

The initial managed implementation may accept the measured amplification.
Future storage optimization must preserve LORE-backed identity, integrity,
history, backup, restore, and promotion semantics. Referenced or external-blob
modes require a separate product decision rather than an implicit fallback.
