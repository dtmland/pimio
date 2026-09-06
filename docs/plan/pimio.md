# pimio

**pimio** is a photo and video organization app focused on helping users build
and maintain chronological media libraries, built on a durable,
version-controlled storage architecture.

## Vision

The app is less about photo touch-ups and more about photo organization. Its primary goal is to help users take large sets of photos and organize them accurately in time, even when original metadata is missing, incomplete, or incorrect.

The interface should feel fluid and simple, similar in spirit to Google Picasa.

Beneath that simple experience, pimio is a versioned digital asset management
platform. It combines a friendly media-library experience with durable,
version-controlled storage based on LORE, providing preservation, history,
portability, backup, and — eventually — collaboration capabilities that
traditional photo managers lack.

## Core Concepts

### Library

The **Library** is pimio's fundamental unit of organization and the only
storage concept the user needs. A v1 library contains managed original
photographs and videos, plus versioned metadata, albums, tags, ratings,
organizational information, modified versions, and application-generated
derivatives such as thumbnails and previews.

### Library = LORE Repository

The central architectural principle is:

> **One pimio Library corresponds to exactly one LORE repository.**

LORE provides durable storage, version history, content deduplication, and
future synchronization. pimio provides the media-specific semantics: LORE
knows a file changed between revisions; pimio knows that IMG_4821 is an
original photograph and revision 17 is the edit with a crop and exposure
adjustment. This separation keeps media semantics out of the storage engine.

A library has a **stable unique identity independent of its physical
location**, so pimio recognizes its repository after it is moved, copied,
backed up, restored, or re-hosted. The repository contains the managed
originals and canonical state needed for a complete portable v1 backup. The
Library Manager handles that unit so users do not manage media directories,
database files, caches, or version-control internals separately.

### Durable versus rebuildable data

The LORE repository is the source of truth for library identity, canonical
records, organization, edit recipes, history, and managed original bytes.
SQLite indexes, thumbnail caches, search indexes, and future face/AI indexes
are derived, disposable, and rebuildable from the repository.

### LORE is invisible

The typical user thinks in libraries, photos, videos, edits, albums, and
history — never repositories, commits, branches, or caches. LORE is exposed
only where it provides value to advanced users (for example optional `lore`
CLI inspection).

## Architectural Layers

pimio maintains a strong boundary between three layers so the application can
exploit LORE without being coupled to it:

1. **UI and application layer** — photos, videos, albums, metadata, editing,
   search, organization, user experience.
2. **Service layer** — media processing, indexing, derivative generation,
   and, in later versions, authentication, permissions, collaboration, and
   the client/server API.
3. **LORE storage layer** — durable content, version history, repository
   management, deduplication, integrity, and synchronization.

## Deployment Progression

The same Library model scales through progressively more capable
configurations without changing the fundamental architecture:

1. **Standalone desktop (v1)** — pimio and `liblore` operate in-process against
   a local, offline repository. No LORE server process or network is required.
   Before release, a feasibility gate proves that this locally originated
   Library can later be promoted to a server without changing identity or
   losing history.
2. **Home server (v2)** — a headless pimio Server hosting multiple
   independent libraries for a household's desktops, laptops, and phones.
3. **Studio (v3)** — the same server architecture with user accounts,
   permissions, review/approval workflows, and collaboration.
4. **Multiple servers (v4+)** — one pimio installation connected to several
   servers, replication, and cloud hosting.

Remote connections distinguish three explicit modes: **open remotely** (the
server stays authoritative), **make a local copy** (offline work, migration,
recovery), and **mirror/synchronize** (exposed only to the extent LORE's real
synchronization semantics support it). A **Library Manager** presents all
libraries — local or remote — as portable first-class objects; location is an
implementation detail. Direct connection to a bare LORE server (no pimio
Server) remains an optional advanced path for recovery, migration, and
tooling.

## Core Goals

- Emphasize chronological photo and video organization
- Make it easy to work with large photo libraries
- Provide best-effort tools for reconstructing or correcting timestamps
- Keep advanced metadata capabilities available without overwhelming the user
- Support both images and videos throughout the app

## Key Features

### Chronological Organization

- Create and manage photo libraries centered around time-based organization
- Allow users to reorder a photo or group of photos by dragging and dropping them within a chronological timeline
- Detect groups of photos that may be related by time or location to assist with organization
- Provide tools to apply best-effort timestamps when original metadata is missing or incorrect
- Optionally apply timezone offsets to datetimes using GPS location and timezone data, including correct DST handling based on place and year

### Location and Mapping

- Include a map view for GPS tagging of both images and videos
- Support landmark detection to help infer or refine GPS location tagging
- Explore support for videos with multiple GPS coordinates over time, if a suitable metadata standard exists

### Media Editing

pimio preserves originals rather than destructively modifying them. Where
practical, edits are represented as operations or recipes rather than
immediately generating new copies of large media files; rendered derivatives
are generated when needed. This reduces storage consumption while preserving
the complete editing history.

pimio explicitly models the relationships between an original asset and its
derivatives (previews, display versions, thumbnails, exports, video proxies,
and trims) rather than inferring them from filenames. LORE stores and
versions the files; pimio owns the semantic relationships between them.

Support basic editing features for both images and videos, including:

- Cropping
- Rotating
- Creating duplicates

For video specifically:

- Clip large videos into shorter cuts
- Support automatic scene detection to assist the clipping process

### Metadata Experience

- Read and write a rich set of metadata types
- Keep metadata presentation simple by default
- Allow users to inspect and edit deeper metadata only when they want to

### Intelligent Assistance

- Automatically detect groups of related photos based on time or space
- Support some level of offline facial detection
- Allow face tracking over time, including from younger to older appearances, to help with timestamping and organization
- Support some level of offline landmark detection to aid location tagging

## Storage and File Management

The core store is built on the **LORE version control system** while remaining
fully abstracted from the user. Standalone v1 loads `liblore` locally and
offline; later deployments may use a LORE server behind pimio Server. LORE is accessed
only through a pimio storage abstraction so LORE-specific concepts do not
leak into the application, and the dependency remains replaceable. The
`.ini`-inspired approach is retired; portable sidecar/embedded metadata
writes remain as an interoperability feature, not the system of record.

## Export

- Provide export options that preserve or adapt timestamps correctly for platforms like:
  - Google Photos
  - iCloud Photo Library

## Design Principles

- Simple, fluid, and approachable UI
- Organization-first rather than editing-first
- Powerful metadata handling without exposing unnecessary complexity
- Best-effort automation that helps users recover and organize imperfect archives
- Never make users understand the infrastructure: the architecture may become
  sophisticated, but a user should be able to think "this is my Family
  Library" without knowing whether it lives on their laptop, a home server,
  or a studio server
- Build the durable foundation (identity, history, storage abstraction, API
  boundaries) early; delay workflow complexity until real needs justify it

## Open Questions

- What is the best metadata standard for storing multiple GPS coordinates across a video's timeline?
- How much facial recognition and landmark detection can be done effectively offline?
- Version 1 uses managed libraries: LORE stores original media with canonical
  state and history. Increment 7.8c accepts the measured storage amplification
  in exchange for a self-contained Library lifecycle. See
  [decision 0005](../decisions/0005-managed-versus-referenced-originals.md).
