# pimio 1.0.0 Release Plan

Implementation is intentionally split into testable, session-sized increments
in [pimio-v1-implementation.md](pimio-v1-implementation.md). That plan also
records the pre-implementation decisions and the required Linux, Windows, and
macOS GitHub Actions evidence.

## Release Goal

pimio 1.0.0 is a native, local-first photo and video organizer for repairing
and maintaining chronological media libraries. It prioritizes dependable
indexing, portable metadata, timestamp correction, and responsive everyday
workflows over a custom graphics engine.

Architecturally, 1.0.0 is the **standalone desktop** stage of the
[deployment progression](pimio.md#deployment-progression): pimio loads
`liblore` in-process and uses a local, offline repository on one computer, for
one user. It does not run an embedded LORE server. The
release should be intentionally modest in visible functionality but ambitious
in architecture — the foundations laid here (library/asset/revision/author
identity, storage abstraction, canonical-versus-derived separation, service
boundaries) must not require a rewrite when v2 adds servers and v3 adds
collaboration. The guiding distinction is between "not implemented yet" and
"architecturally impossible"; v1 must always land on the former.

The application should use Qt 6, C++, and QML. The initial interface uses
standard Qt controls and virtualized QML grid, list, timeline, and detail
views. This is intentionally a basic frontend: the library, metadata, jobs,
and media-request interfaces must not depend on a particular QML view so they
can support the 2.0.0 rendering frontend without being rewritten.

## Product Scope

### Library and Resilience

- Treat the **Library** as the fundamental, user-facing unit: one library is
  one LORE repository with a stable unique identity independent of its
  location on disk. Provide library management — create, open, close, switch,
  rename, move, back up, and restore libraries — so a library is a portable,
  explicit object rather than an implicit side effect of scanned folders.
  Version 1 references originals in configured media roots, so a portable
  backup must include those roots as well as the repository. Increment 7.8c
  confirmed this choice after removal of the whole-store rollback copy;
  Library Manager must enumerate included and excluded roots.
- Configure one or more local media roots per library.
- Scan images and videos incrementally, preserving a stable file identity and
  content fingerprint to recognize moves and renames.
- Store derived state in a SQLite cache with migrations, transactions,
  write-ahead logging, and a persistent job queue.
- Treat LORE as the ground-truth storage layer for durable state and history.
  Treat the SQLite index and generated cache as rebuildable: a corrupted or
  deleted index is reconstructed from the repository, never the reverse.
  Preserve portable metadata and edit recipes with the original media.
- Watch configured folders, coalesce filesystem events into durable jobs, and
  run low-priority reconciliation scans for watcher overflows, network volumes,
  or missed events.
- Show library health: unavailable roots, watcher problems, failed jobs, cache
  size, low-space conditions, and sidecar conflicts.

### Metadata and Chronological Organization

- Extract image and video metadata, including filenames, folders, capture
  timestamps, camera fields, duration, rotation, audio presence, GPS, tags,
  captions, and ratings where available.
- Write portable state to embedded EXIF/IPTC/XMP where safe and to adjacent XMP
  sidecars for RAW files or non-destructive changes. Keep application-only UI
  state and cache references in SQLite.
- Surface metadata simply by default, with an inspector for detailed fields.
- Provide chronological timeline browsing, date/type/rating/camera/location
  filtering, and search across filenames, folders, tags, captions, people, and
  metadata.
- Provide batch timestamp repair, drag-and-drop chronological reordering, and
  best-effort timezone correction using GPS location, place, year, and DST
  rules. Present uncertain inferences for user confirmation rather than silently
  changing metadata.
- Detect potentially related groups by time and location as suggestions.

### Images

- Generate persistent image thumbnails and previews in background jobs, using
  embedded RAW previews and decode-time shrinking where practical.
- Present a virtualized image/video grid and timeline using standard QML
  components, requesting only visible thumbnails plus a small prefetch margin.
- Offer a detail viewer with progressive preview loading.
- Support non-destructive crop, orientation, rotation, and duplicate workflows.
- Store edits as ordered, versioned recipes rather than UI or shader commands.
- Export edited copies while retaining originals; use lossless JPEG transforms
  where the source and operation permit them.

### Video

Video is a 1.0.0 feature, not a deferred capability.

- Provide integrated video playback with a reliable cross-platform decoding
  fallback and hardware acceleration where available.
- Extract video thumbnails in background jobs, avoiding unusable black frames
  where practical, and display duration and playback indicators in the library.
- Support non-destructive in/out trimming with an interactive timeline.
- Provide scene detection to identify likely clip boundaries and present them as
  editable suggestions.
- Export trimmed video losslessly only when requested boundaries and the source
  container/keyframes support stream-copy output; otherwise clearly label the
  result as re-encoded.
- Keep trimming and other video edits as recipes, so playback and exports use
  the same source of truth without changing the original.

### Location

- Provide a basic map view for viewing existing GPS locations and manually
  assigning or correcting locations on images and videos.
- Keep map use optional and make failures to load map data non-blocking.
- Defer offline map caching, large-library clustering, GPS-track display,
  reverse geocoding, landmark suggestions, and complex timeline/map
  interactions to 2.0.0.

## Architecture Boundaries

The following services are UI-independent and form the stable contract for
2.0.0:

- Library queries and virtualized media models.
- Metadata reading, conflict handling, and portable writes.
- Thumbnail, preview, and video-frame requests.
- Persistent priority job queue, cancellation, progress, and failure reporting.
- File watching and reconciliation.
- Edit recipes, playback ranges, and export rendering.
- Search and chronological grouping.

pimio additionally maintains the three-layer separation described in
[pimio.md](pimio.md#architectural-layers): UI/application, services, and LORE
storage. LORE is reached only through the `pimio::core::DurableStore`
abstraction; no other component may know LORE exists.

### Foundation Requirements for Future Versions

v1 is single-user and local, but the following concepts must be implemented
correctly from the beginning because they are prohibitively hard to retrofit:

- **Library identity.** Every library carries a persistent unique identifier,
  stored inside the repository, that survives copying, backup, restoration,
  migration, and relocation. A library's path or network location is never
  its identity.
- **Asset identity.** Every media asset has a stable identity independent of
  filename and filesystem location, under which its original, derivatives,
  metadata, and revisions are grouped.
- **Revision identity.** Every meaningful modification produces an
  identifiable revision recording its parent revision, author, timestamp,
  application version, and change information.
- **Author identity.** Every revision carries an author field
  (`author = user id`), even though v1 has exactly one implicit local user.
  Multi-user history in v3 must not require rewriting v1 history.
- **Authorization boundary.** The service layer has a conceptual
  authorization model (read / write / administer / share per library) that
  v1 satisfies trivially by granting the local user everything. v2/v3 make it
  configurable without moving the boundary.
- **Immutable history.** Canonical revisions are never destructively
  modified; changes append revisions. This is what makes future
  collaboration, auditing, rollback, and conflict handling tractable.
- **Explicit asset relationships.** Original-to-derivative relationships
  (thumbnail, preview, export, proxy, trim) are modeled in the data, not
  inferred from filenames.
- **Rebuildable indexes.** Every search/index/cache database can be destroyed
  and reconstructed from the repository. This is a fundamental v1 property,
  not an optimization.
- **Service API boundary.** Application logic reaches library services
  through an in-process API designed so that v2 can put a network between
  client and services (headless pimio Server, mobile clients) without an
  architectural rewrite. v1 does not implement networking, authentication,
  or user management UI — only the boundaries they will need.
- **Server promotion path.** Before release, an automated feasibility gate
  proves that a Library created offline can be registered with a LORE server,
  pushed with its complete identity and history, and cloned back without
  changing its pimio library identity. This validates the storage path needed
  by a future pimio Server; it does not add that server product to v1.

Deliberately postponed (not designed out): complex permissions, team
administration, approval workflows, asset locking, client portals,
multi-site replication, sophisticated conflict resolution, and distributed
deployment. See [pimio-v2.md](pimio-v2.md) for when each arrives.

### Versioning and Commit Strategy (LORE Ground Truth)

- Use LORE as the authoritative storage layer for library state, metadata
  changes, tags, timestamps, and edit recipes. Use SQLite as an ephemeral
  read/query cache rebuilt from LORE.
- Keep commits user-initiated rather than background-batched. Expose explicit
  "Save" affordances (including per-group checkpoints) so each LORE commit
  maps to a meaningful user action. Scan/ingest results are committed in
  batches as application checkpoints.
- Record author, timestamp, application version, and change description with
  each checkpoint so revisions satisfy the identity requirements above.
- Stage metadata edits, tag changes, timestamp repairs, and recipe updates in
  SQLite during interaction, then commit them to LORE when the user saves.
  Handle failed LORE commits visibly and recoverably without destructive data
  loss.
- Serve interactive queries from SQLite for responsiveness. Rebuild the SQLite
  cache from LORE on startup and after detected external repository changes.
- Allow optional direct LORE access for power users (for example through
  `lore cli`) to inspect or modify repository state; detect those modifications
  and rehydrate the SQLite cache accordingly.
- Treat SQLite loss or corruption as recoverable by rebuilding from LORE.
  Rely on LORE commit history for auditability and user-visible undo points.
- This model keeps v1 local-first while establishing LORE as the future sync
  point for multi-client workflows in 2.0+.
- This differs from a pure SQLite design by moving durable history and
  checkpoint semantics out of the local cache. It differs from a pure `.ini`
  design by retaining fast indexed queries through SQLite while using LORE as
  the durable versioned source of truth.

Prioritize visible thumbnails and direct interaction above viewer decoding and
export; prioritize those above scans, scene detection, and other optional
analysis. Bound worker concurrency, decoding memory, and queues. Cancel stale
requests as views or selections change.

## Distribution and Licensing

Maintain a dependency bill of materials that records each library, enabled
features, license, source location, notices, modifications, and redistribution
status. Review both source-code and model-weight licenses before distribution.

- **LGPL dynamic-link path:** a dependency may usually be delivered beside the
  app as a separate DLL, `.dylib`, or `.so`; users do not normally need to
  download it themselves. Required notices and license text must be included,
  and users must retain the practical ability to replace or relink that library.
  Static linking usually needs additional compliance work or a commercial
  license.
- **GPL linked-library path:** linking a GPL dependency into the application
  normally requires the combined distributed application to be GPL-compatible.
  Do not choose this path until pimio's own distribution license is settled.
- **Separate-process path:** a GPL program can sometimes be offered as a
  separately installed or separately invoked tool, but this is not an automatic
  exemption from licensing obligations. Obtain legal review for the intended
  packaging and integration.
- **Commercial-license path:** use vendor licensing when closed distribution,
  static linking, or simplified operational obligations justify it.

For video, FFmpeg's license depends on its selected configuration; enabling GPL
components changes the distribution analysis. HEVC/HEIC support can also carry
patent licensing considerations. These decisions require legal review before a
release.

## Release Acceptance

1. A user can index a large mixed image/video library without blocking normal
   browsing.
2. A user can repair timestamps, edit metadata, crop/rotate images, trim
   videos, review scene suggestions, and export results without altering
   originals unintentionally.
3. Reindexing restores portable metadata and edit recipes after deleting the
   derived cache.
4. The app handles interrupted work, unavailable folders, event loss,
   unsupported media, low disk space, cancellation, and metadata conflicts
   visibly and recoverably.
5. A user can create, open, switch, back up, and restore libraries; a
   restored or moved library is recognized as the same library, and pimio
   reconstructs its index and caches from the repository.
6. Native builds are tested on supported Windows and macOS versions and a
   representative X11/Wayland Linux environment.
7. A Library created locally/offline can be promoted to a test LORE server and
   cloned into a fresh location with the same library identity, revision
   history, and content.

After 1.0.0, the 1.x series focuses on making the library extremely
dependable before v2 introduces servers: robust backup and restore
verification, library integrity checking, migration between computers,
recovery from interrupted operations, background indexing, storage
monitoring, cache management, and improved version history. A user must be
able to trust a pimio Library before pimio adds collaboration.

These are release-level outcomes, not a single implementation milestone.
Feature work must pass the per-increment gates in the implementation plan.
In particular, production persistence work is blocked on proving the proposed
LORE contract, and hosted headless CI does not replace manual testing of real
desktops, GPUs, signing, or installer security prompts.
