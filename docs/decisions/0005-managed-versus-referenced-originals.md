# 0005 — Referenced originals for v1

Status: **superseded as a v1 conclusion by
[decision 0006](0006-local-first-lore-topology.md); evidence retained.**
Recorded at the end of Increment 7.8.

## Decision

pimio initially chose referenced libraries for v1: LORE stores the library
descriptor, metadata, organization, edit recipes, and history, while original
media remains in configured media roots.

This conclusion is reopened because pimio will remove the whole-store recovery
copy that was its principal no-go reason. The current implementation remains
referenced until Increment 7.8c decides whether v1 uses managed originals,
referenced originals, or both.

## Context

The library-centric design aims to make a Library portable and self-contained,
but the existing implementation versions JSON records and references original
media in place. Increment 2 measured LORE with small records only. Increment
7.8 therefore tested whether LORE 0.8.5 can carry compressed-media-sized binary
content before any managed ingest was designed.

The spike is `lore.binary_content`. It creates deterministic, effectively
incompressible binary content to model the storage characteristics of a
compressed video, commits it through the independently published LORE CLI,
starts a fresh CLI process to restore it, verifies its SHA-256, and commits the
same content at a second path to measure deduplication. Its default 8 MiB size
keeps normal CI economical. Set `PIMIO_LORE_BINARY_SPIKE_MIB=256` to reproduce
the multi-hundred-MB run.

## Measurements

The 256 MiB run was performed on 2026-09-01 on the Linux x86-64 task runner
with the pinned LORE 0.8.5 CLI and local, offline, sync-data operation.

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

## Why managed originals were a no-go

Binary correctness and LORE's content deduplication passed. The current pimio
durability strategy makes the full design unsuitable for a photo/video library:

1. A checked-out original also exists in LORE's immutable store. The first
   256 MiB payload therefore occupied about 512 MiB before small repository
   overhead.
2. Before every pimio commit, `LoreDurableStore` copies the complete `.lore`
   directory to a recovery backup. This is required to uphold Increment 2's
   durability boundary around LORE 0.8.5. With managed originals, even a tiny
   metadata edit would require time and temporary free space proportional to
   the entire media corpus.
3. A library-scale corpus is commonly hundreds of gigabytes or terabytes.
   Requiring its immutable content to be copied for each metadata checkpoint is
   incompatible with interactive saves and predictable low-space behavior.

The gate was intentionally about the complete pimio storage path, not whether
LORE can read one large file in isolation. Passing byte integrity while failing
the required commit/recovery economics is a no-go.

## Backup, restore, and portability consequences

A v1 library backup is complete only when it includes both:

- the LORE repository, which preserves identity, canonical records, and
  history; and
- every referenced media root, with enough mapping information to reconnect
  restored paths.

Backing up only the repository is an **organizational-state backup**, not a
portable copy of the original media. Library Manager workflows in Increment
7.9 must either include referenced roots in a backup archive or clearly
enumerate which roots were excluded. Restore must preserve the library id and
report missing roots instead of implying that their media was backed up.

Moving or copying the repository preserves library identity, but does not make
the original media self-contained. Product documentation and UI must use
"portable library" only when the referenced roots accompany the repository.

## Alternatives considered

- **Managed originals in LORE for v1.** Rejected because checkout duplication
  and pimio's full recovery backup make every commit scale with corpus size.
- **Managed and referenced modes in v1.** Rejected because the managed half has
  the same unresolved storage economics and would double lifecycle complexity.
- **Bypass the recovery backup for binary content.** Rejected because it weakens
  the accepted durability boundary and re-exposes LORE's known interrupted
  commit failure modes.
- **Referenced originals.** Accepted. It retains the proven metadata/history
  architecture while making backup scope explicit.

## Revisit criteria

A future managed mode needs a feasibility gate covering large real-world
corpora, low-space and interrupted-write behavior, backup/restore, and all
supported platforms. Candidate designs may use immutable blobs outside the
LORE checkout or a newer LORE transaction model, but must preserve content
integrity without a whole-corpus copy on ordinary metadata commits.

Increment 7.8c performs that revisit after the 0.9.0 migration and rollback
removal. The remaining checkout-plus-immutable-store duplication must be
measured and accepted or mitigated independently of the removed workaround.
