# 0005 — Referenced originals for v1

Status: **accepted.** Initially recorded at the end of Increment 7.8 and
re-evaluated through the production LORE 0.9.0 path in Increment 7.8c.

## Decision

pimio uses referenced libraries for v1: LORE stores the library
descriptor, metadata, organization, edit recipes, and history, while original
media remains in configured media roots.

Managed originals and a choice between managed and referenced modes remain out
of v1. Removing pimio's whole-store recovery copy makes small metadata commits
independent of corpus size, but it does not remove LORE's checkout plus
immutable-store duplication. Referenced originals therefore retain predictable
capacity requirements while the Library Manager makes backup scope explicit.

## Context

The library-centric design aims to make a Library portable and self-contained,
but the existing implementation versions JSON records and references original
media in place. Increment 2 measured LORE with small records only. Increment
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

## Why managed originals remain out of v1

Binary correctness, restart, restore, and LORE's content deduplication pass.
The removed rollback copy also means an ordinary pimio metadata commit no
longer copies the corpus. The remaining costs still make managed originals a
poor v1 default:

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
   interrupt each binary-write phase, so the managed candidate lacks that
   evidence; the selected referenced path does not perform those writes.
4. Managed ingest, lifecycle policy, partial backup, and storage monitoring do
   not otherwise benefit v1's organization workflows enough to justify a
   second storage mode and its cross-platform failure surface.

The gate is intentionally about the complete pimio storage path, not whether
LORE can read one large file in isolation. The 0.9.0 result removes the
repository-sized cost from metadata commits, but the resting, backup, restore,
low-space, and hosted-storage economics still do not justify managed ingest in
v1.

## Backup, restore, and portability consequences

A v1 library backup is complete only when it includes both:

- the LORE repository, which preserves identity, canonical records, and
  history; and
- every referenced media root, with enough mapping information to reconnect
  restored paths.

The Increment 7.9 Library Manager must offer an organizational-state backup of
the repository and a complete backup that includes selected referenced roots.
Every backup manifest must enumerate each root and whether its content is
included. Restore preserves the library id, allows roots to be reconnected at
new locations, and reports missing or excluded roots instead of implying that
their media was backed up.

Moving or copying the repository preserves library identity and does not move
the media roots. Promotion pushes canonical repository state and history; it
does not upload referenced originals. Product documentation and UI may call a
backup or promoted Library self-contained or portable only when the referenced
media is transferred and reconnectable too.

## Alternatives considered

- **Managed originals in LORE for v1.** Rejected after the 0.9.0 retest because
  checkout/store duplication and complete-backup capacity remain, despite
  metadata commits no longer copying the repository.
- **Managed and referenced modes in v1.** Rejected because the managed half has
  the same capacity and low-space concerns and would double lifecycle
  complexity.
- **Referenced originals.** Accepted. It retains the proven metadata/history
  architecture, keeps one live copy of each original, and makes backup and
  promotion scope explicit.

## Revisit criteria

A future managed mode needs a product requirement that outweighs its capacity
cost and a new feasibility gate covering large real-world corpora, deterministic
low-space and interrupted-binary-write behavior, incremental backup/restore,
retention, and all supported platforms. Candidate designs may use immutable
blobs outside the LORE checkout or a newer LORE transaction model, but must
preserve content integrity without requiring two live local copies of the
entire corpus.
