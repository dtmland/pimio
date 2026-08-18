# pimio 2.0.0 Release Plan and Beyond-1.0 Roadmap

## Version Strategy

Post-1.0 development follows the deployment progression in
[pimio.md](pimio.md#deployment-progression). Exact version numbers stay
flexible; the important distinction is between architectural milestones and
marketing releases.

| Version | Primary goal |
| --- | --- |
| 1.x | Reliability and portability: backup, restore verification, integrity checking, migration (see [pimio-v1.md](pimio-v1.md#release-acceptance)) |
| 2.0 | Accelerated frontend; home server and multi-device operation |
| 2.x | Mobile clients |
| 3.0 | Multi-user / studio capabilities |
| 3.x | Advanced collaboration and studio workflow |
| 4+ | Multi-server, replication, cloud deployment |

Throughout every stage, the Library and its LORE repository remain the same
conceptual object; each stage is an increasingly sophisticated way of
interacting with it.

## Release Goal

pimio 2.0.0 has two tracks. The **rendering track** replaces the basic 1.0.0
browsing and preview views with a fluid, GPU-accelerated media experience
while retaining the proven library, metadata, job, edit, and export services
from 1.0.0. It should be driven by measured performance limitations, not by a
requirement to reproduce historical Picasa implementation details. The
**server track** introduces the headless pimio Server and multi-device
operation over the service API boundary established in v1. The tracks are
independent and may ship in either order within the 2.0 cycle.

Qt's rendering abstraction should select the appropriate platform backend:
Direct3D on Windows, Metal on macOS, and Vulkan or OpenGL on Linux. Keep
platform-specific integrations behind adapters rather than adopting a
lowest-common-denominator frontend.

## Rendering and Browsing

- Replace standard QML grid/timeline delegates where needed with custom QML
  scene-graph components that consume the existing media-query and
  thumbnail/preview-request interfaces.
- Provide GPU texture management, virtualized tile rendering, viewport-aware
  prefetching, and strict eviction limits.
- Add smooth pan, zoom, transition, and selection behavior across the library,
  timeline, map, and viewer.
- Generate and request image pyramids or deep-zoom tiles only for media and
  zoom levels where they improve measured responsiveness.
- Use preview-first loading: thumbnail or embedded preview, then a
  color-managed medium preview, then full-resolution source or tiles as needed.
- Apply reduced-image edit previews through the GPU where beneficial; debounce
  controls and cancel obsolete preview work.
- Keep fallback behavior for systems where the preferred graphics backend is
  unavailable.

## Home Server and Multi-Device (Server Track)

A **pimio Server** is a headless deployment of the pimio application with an
embedded LORE server, hosting multiple independent libraries — each its own
LORE repository — for home servers, NAS systems, always-on machines, and
small studios.

Server capabilities:

- Headless operation, multiple libraries, and library management.
- Remote authentication: a real mechanism, not trusted local-network access.
- Library discovery, remote browsing, and remote media access.
- Background processing, indexing, and derivative generation on the server.
- Storage monitoring, backup configuration, server health, and connection
  management.

Desktop capabilities:

- Add a remote server; browse and open its libraries. The Library Manager
  presents local and remote libraries uniformly; a pimio installation may
  connect to multiple servers.
- Three explicit connection modes: **open remotely** (the server stays
  authoritative), **make a local copy** (offline, travel, migration,
  recovery — the copy retains the library's identity), and
  **mirror/synchronize** (offered only to the extent LORE's real
  synchronization semantics support it; a sync-semantics spike is the entry
  gate for this mode).
- Move a library between local and remote storage.
- Optionally connect directly to a bare LORE server (no pimio Server) as an
  advanced path for recovery, migration, and tooling; this mode is more
  limited and never required for ordinary use.

Even while v2 remains personal/family oriented, this is where the multi-user
architecture becomes real: user identities on the server, per-library
membership (owner/editor/viewer), authentication, and the basic permission
model behind the authorization boundary reserved in v1. The UI stays simple;
per-revision authorship recorded since v1 starts naming real users.

## Mobile Clients (2.x)

Once the server API is stable, mobile clients use the same pimio service API
rather than manipulating LORE repositories directly. Candidate capabilities:
camera import, library browsing, favorites, albums, metadata, basic edits,
upload, offline access, and background synchronization. The mobile app never
needs to understand the storage architecture.

## Advanced Maps and Location Workflows

"Advanced maps" means capabilities beyond 1.0.0's basic GPS display and manual
location assignment:

- Offline map-tile caching and controlled cache eviction.
- Clustering and filtering of very large libraries on a map.
- Batch location correction and richer timeline-to-map interactions.
- Reverse geocoding and landmark-assisted location suggestions, subject to
  privacy, offline-data, and licensing decisions.
- Visualization and editing of video GPS tracks when a reliable interoperable
  metadata representation is available.
- Clear confidence and conflict handling for inferred locations.

All location intelligence remains optional, local where possible, and subject
to user confirmation before metadata changes.

## Deferred Intelligence and Editing

- Add opt-in local face detection and, only after licensing review, identity
  clustering. Record the license and redistribution rights of model weights
  separately from those of inference code.
- Offer removable local analysis data, pause controls, and battery/thermal
  limits.
- Evaluate additional non-destructive filters, lens correction, and darkroom
  controls only when their recipes can render consistently in preview and
  export paths.
- Evaluate opt-in sync providers separately from indexing. Synchronize
  originals and portable sidecars conflict-aware, and rebuild derived caches
  locally rather than syncing caches.

## Continuity Requirements

- Do not change the portable metadata, edit-recipe, persistent-job, or export
  contracts merely to support the accelerated frontend.
- Continue to treat watchers as hints and reconciliation scans as the source of
  recovery from missed events.
- Preserve cancellation, bounded resource use, and foreground-work priority
  throughout the new renderer.
- Keep all generated thumbnails, pyramids, map tiles, and GPU caches disposable
  and versioned.
- Test accessibility, native menus, keyboard navigation, drag and drop, HiDPI,
  and fallback rendering alongside visual performance.

## Release Acceptance

1. Scrolling, zooming, and opening media are measurably more responsive than
   1.0.0 on the lowest supported hardware.
2. Large-library texture and cache use remains bounded and recoverable.
3. Custom views use the established 1.0.0 services without duplicating indexing,
   metadata, editing, or job logic.
4. Advanced maps and optional intelligence are safe to pause, remove, and
   recover from without endangering the library.
5. The accelerated frontend and fallbacks are tested natively on supported
   Windows, macOS, and Linux graphics environments.
6. A library hosted on a pimio Server can be browsed remotely, copied locally
   while retaining its identity, and moved between local and remote storage;
   the server enforces authentication and basic per-library permissions.

## Studio and Collaboration (3.0 and 3.x)

v3 introduces true multi-user collaboration on the same server architecture —
a matter of scale and features, not a different storage model.

- **Users and roles.** Flexible role model (owner, administrator, editor,
  contributor, reviewer, viewer). Permissions may extend from server and
  library level toward album or asset level only if real workflows justify
  the granularity.
- **Attribution and audit.** Who changed an asset, what changed, when, and
  which revision resulted — built on the per-revision author identity
  recorded since v1.
- **Conflict handling.** Introduced only after the versioning model is
  proven. The server serializes writes above LORE (per
  [decision 0001](../decisions/0001-lore-durable-store.md), concurrent
  writers must never reach LORE), so conflicts are an application concept:
  parallel revisions of the same asset can be kept, one selected as
  canonical, compared, merged into a new revision, or sent to human review.
  Avoid automatic media merging unless the media type makes it safe.
- **Workflow (3.x).** Review queues, approval/rejection, comments,
  assignments, shared collections, client review portals, publishing
  workflows, revision comparison, and change notifications — all
  application-layer features, never responsibilities of LORE.

## Advanced Deployment (4+)

Only once the client/server model is mature: server-to-server replication,
disaster-recovery replicas, geographic redundancy, cloud hosting, selective
synchronization, off-site backup, and multi-location studios.

## Deliberately Postponed

To keep each release achievable, the following are postponed but must never
be made architecturally impossible: complex permissions, team
administration, approval workflows, asset locking, client portals,
multi-site replication, sophisticated conflict resolution, enterprise
administration, and distributed cloud infrastructure. The v1 identity,
history, and API foundations exist precisely so these remain "not
implemented yet" rather than "impossible".
