# libumm Feature Requests

Ready-to-file feature requests for
[dtmland/libumm](https://github.com/dtmland/libumm). The status of each
request is tracked in [libumm-upstream-requests.md](libumm-upstream-requests.md),
which keeps the short `U#` table. This file holds the full text you paste
into each upstream issue.

These requests exist because pimio does not reimplement metadata behavior
([0008](../decisions/0008-libumm-metadata-engine.md) §1). Where libumm lacks
something, pimio asks libumm for it and adopts the release that delivers it.
pimio does not build a workaround unless the maintainer approves one, with a
recorded removal condition.

The copy to give the libumm maintainer is
[libumm-feature-requests-upstream.md](libumm-feature-requests-upstream.md).
That file must stay free of pimio names, links, milestones, and interim
notes. Update it whenever a request here changes.

Evidence comes from the libumm **v0.1.2** source tag (the planned pin). Header
citations are `include/umm/<file>:<line>` in that tag. Recheck each request
against the current pin before filing, and record the upstream issue number
in the tracker once filed.

## How pimio's mission drives these requests

pimio is a local-first, time-centric photo and video organizer
([pimio-v1.md](pimio-v1.md)). In metadata terms that means:

- **Large libraries, kept fresh.** Scan and watch tens of thousands of files
  on background workers. The work must be cancellable and must not block the UI.
- **Time is the primary axis.** Capture time drives ordering, and users must
  be able to repair wrong camera clocks without losing precision or inventing
  time zones.
- **Photos and videos behave alike.** One editor, one set of fields, and the
  same filters for both media types.
- **Location and people** in standard, portable fields that other tools can read.
- **Honest UI.** Controls are disabled with a reason, never silently ignored.
- **Crash safety.** pimio never acknowledges a save that is not durable.

## Priority key

| Priority | Meaning |
| --- | --- |
| **Blocking** | A migration increment cannot ship as planned without it, or must ship with a recorded degradation. |
| **Important** | v1 works without it, but with a visible cost (speed, UX, or a feature gap). |
| **Nice to have** | Post-v1 or convenience. |

## Summary

| # | Request | Priority | Verified against v0.1.2 | Blocks |
| --- | --- | --- | --- | --- |
| [U1](#u1-concurrent-reads--thread-safety-contract) | Concurrent reads / thread-safety contract | **Blocking** for scan throughput | Confirmed gap | M3, Inc 10 |
| [U9](#u9-cancellation-and-deadlines-per-call) | Cancellation and deadlines per call | Important | Confirmed gap | M3, M5, jobs |
| [U10](#u10-selective-and-batch-reads-for-scanning) | Selective and batch reads for scanning | Important | Confirmed gap | M3 |
| [U4](#u4-per-property-capability-with-a-reason) | Per-property capability with a reason | **Blocking** for M6 | Confirmed gap | M5, M6 |
| [U8](#u8-datetime-shift-helpers) | `DateTime` shift helpers | Important | Confirmed gap | Inc 9 |
| [U2](#u2-typed-read-only-technical-fields) | Typed read-only technical fields (photo) | Important | Confirmed gap (by design, C17) | M3, M4 |
| [U3](#u3-video-technical-properties-without-exiftool) | Video technical properties without ExifTool | Important | Deferred upstream ("later") | M3, Inc 10 |
| [U11](#u11-durable-write-option) | Durable write option (fsync) | Nice to have | Confirmed gap | In-place writes |
| [U12](#u12-legacy-and-hierarchical-keyword-sources) | Legacy and hierarchical keyword/title sources | Important | Deferred upstream ("later") | Import of existing libraries |
| [U13](#u13-people-regions-cross-media) | People regions, cross-media | Nice to have (post-v1) | Deferred upstream ("later") | Face tagging |
| [U14](#u14-payload-fingerprint-excluding-metadata) | Payload fingerprint excluding metadata | Nice to have | Confirmed gap | Duplicate detection |
| [U5](#u5-read-from-bytes-or-a-stream) | Read from bytes or a stream | Nice to have | Confirmed gap | Tests only |
| [U6](#u6-canonical-mappings-for-more-types) | Canonical mappings for BMP, MKV, AVI, WAV, ASF | Nice to have | Confirmed gap | Editing those types |
| [U7](#u7-application-packaging-and-exiftool-configuration-guidance) | Packaging and ExifTool configuration guidance | Important | Partly documented | M7 |

Already in libumm, so **not** requested (pimio must use these as they are):
GPS track import and capture-time matching (`importTrack`, `matchTrack` in
`track.hpp`), cross-media `rating()` and `personShown`, atomic temp-plus-rename
writes, `WriteOptions::dry_run` with a `WriteReport::written` key list,
`ReadOptions::report_casts`, `OffsetTimeOriginal` folded into the canonical
`dateCreated` offset, and `dumpAll()` / `dumpUnmapped()` for the read-only view.

---

## U1. Concurrent reads / thread-safety contract

**Priority:** Blocking for scan throughput. pimio can ship M3 serialized
(see Interim), but large-library scans will be slower.

**pimio use case.** Scan, watch, and background jobs read metadata for many
files at once on a bounded worker pool. Today every metadata read in pimio
must go through one worker, so metadata extraction becomes the scan bottleneck,
and a single slow ExifTool read (large video) stalls every other read.

**Current libumm behavior (v0.1.2).**

- `include/umm/backend.hpp:11-13`: "each Backend instance is single-threaded.
  Callers must not share an instance across threads. BackendManager may pool
  instances later; this session's manager holds one instance per registered id."
- `BackendManager::instance()` (`backend.hpp:126`) is a process singleton that
  `umm::read` / `umm::write` use internally. Callers cannot choose an
  instance, so two threads calling `umm::read` share the same Exiv2 adapter and
  the same ExifTool `-stay_open` process.
- No locking appears in the public headers, and the thread-safety of the free
  functions (`read`, `write`, `capabilities`, `describe`, `cast`) is not
  documented.

**Requested.** One of these, documented in the public headers:

1. **Preferred:** `BackendManager` pools backend instances, with a configurable
   pool size (one ExifTool process per pooled instance), and the free
   functions become safe to call from multiple threads.
2. **Minimum:** the free functions serialize internally (they are documented as
   thread-safe, but run one at a time). pimio can then drop its own
   serialization, and pooling becomes a later improvement.
3. **Alternative:** an explicit `Session`/`Context` object that owns its own
   backend instances, for which `read`/`write` overloads exist. One per worker
   thread.

Also state which functions need no backend at all (for example `describe`,
`cast`, and DateTime helpers) and are therefore always thread-safe.

**Acceptance criteria.**

- Thread-safety is stated for every public entry point.
- A libumm test reads N fixtures from M threads in parallel (under TSan in
  CI) and gets results identical to serial reads, for both the Exiv2 and the
  ExifTool backends.
- Pooled ExifTool processes are bounded, are reaped on shutdown, and respect
  `command_timeout`.

**Interim in pimio.** All libumm calls go through one metadata worker
([0008](../decisions/0008-libumm-metadata-engine.md) §8). That worker is the
only code that touches libumm types, so adopting the fix means changing the
worker count, not the call sites.

## U9. Cancellation and deadlines per call

**Priority:** Important.

**pimio use case.** Viewport changes, library close, and app shutdown must
cancel queued and running metadata work quickly
([pimio-v1.md](pimio-v1.md): "Persistent priority job queue, cancellation,
progress"). With one serialized worker (U1), a long ExifTool read on a large
video delays everything behind it, including the shutdown.

**Current libumm behavior.** `ReadOptions` and `WriteOptions`
(`umm.hpp:39-58`) have no cancellation token or deadline. The only bound is
the process-wide `ExifToolConfig::command_timeout` (30 s default,
`backend.hpp:121`), which kills and restarts the ExifTool child.

**Requested.** An optional per-call cancellation token (a callable or atomic
flag) and an optional deadline in `ReadOptions`, `WriteOptions`, and
`SyncOptions`. A cancelled call returns a distinct `ErrorCode` (for example
`cancelled`). A cancelled write must leave the original untouched, which the
existing atomic temp-plus-rename write already makes possible.

**Acceptance criteria.** Cancelling an in-flight ExifTool read returns within
a bounded time, does not leave a zombie process, and the next call succeeds.
Cancelling a write leaves the original file byte-identical.

**Interim in pimio.** Cancel only between calls. Shutdown waits for at most one
in-flight call, bounded by `command_timeout`.

## U10. Selective and batch reads for scanning

**Priority:** Important. Measure the gain in the M0 spike before filing.

**pimio use case.** A first scan of a large library needs only a few
properties per file to build the timeline: `dateCreated`, location, rating,
and (once U2 lands) orientation and pixel size. The full canonical read,
unmapped dump, and sidecar pairing can wait until the user opens the
inspector.

**Current libumm behavior.** `umm::read` (`umm.hpp`) always performs a full
read: every canonical property, `dumpAll()`, and sidecar merging when
`merge_sidecar` is true. No property filter or multi-file entry point exists.

**Requested.**

- `ReadOptions::properties`: an optional allow-list of canonical ids. When set,
  libumm may skip work that is not needed for them, including the unmapped
  dump.
- An optional `readMany(paths, options, callback)` that streams one result per
  path, so the ExifTool backend can batch its requests.

**Acceptance criteria.** A benchmark in libumm shows that a selective read is
measurably faster on JPEG/HEIC/MP4 fixtures. Results for the requested ids
are identical to a full read.

**Interim in pimio.** Full reads, serialized (U1). The read-only "All
metadata" view uses the same result, so nothing is read twice.

## U4. Per-property capability with a reason

**Priority:** Blocking for M6 (editor gating).

**pimio use case.** The editor shows the canonical properties for the selected
items, which may mix photos and videos. Each control must be enabled only when
that property can be written to every selected file with the embedded-only
policy and the backends available now (for example, when ExifTool is
missing). When a control is disabled, the UI must show why.

**Current libumm behavior.** `capabilities()` (`capabilities.hpp:62`) reports
per backend and per *category* (`exif`, `iptc_iim`, `xmp`, `icc`,
`thumbnail`) plus `LocationAccess`, `identify_only`, free-text `notes`,
`sidecar_recommended`, and `preferred_backend` (`capabilities.hpp:18-58`). It
does not say whether a given canonical property (for example
`iptc.video.headline`) is readable or writable for this path, under this
policy, with the backends that are available now.

**Requested.** A function such as
`propertyAccess(path, property_id, StoragePolicy) -> {Access, backend, reason}`,
or a bulk variant that returns every canonical id for a path. `reason` is a
stable code plus readable text, for example `exiftool_unavailable`,
`policy_requires_sidecar`, or `no_mapping_for_type`.

**Acceptance criteria.** For every supported type, the result agrees with
what `write(..., dry_run = true)` would do. The reason codes are documented and
stable within a minor version.

**Interim in pimio.** None. pimio must not derive per-property access from
category tables, because that would duplicate libumm's mapping knowledge. If
this request is not delivered, M6 ships with `dry_run` probing per property
only after the maintainer approves that interim.

## U8. DateTime shift helpers

**Priority:** Important (Inc 9 batch time shift is a core mission feature).

**pimio use case.** "Camera clock was 1 h 12 min fast", or "these were taken in
UTC+2". pimio applies a shift to many items at once, photos and videos alike.

**Current libumm behavior.** `umm::DateTime` (`value.hpp:30`) carries partial
precision (year required; month through sub-second optional) and an optional
UTC offset, where absent means unknown, not UTC (H12). There are no
arithmetic, shift, or offset-change helpers.

**Requested.**

- `shift(DateTime, std::chrono::seconds)`, which keeps the existing
  precision. Define the behavior when the value has no time component, for
  example by rejecting the shift or requiring day granularity.
- `withOffset(DateTime, minutes, mode)` with two modes: *reinterpret* (same
  wall time, new zone) and *convert* (same instant, new wall time). Converting
  must fail when the offset is unknown.
- Correct handling of calendar rollover across month, year, and leap-day
  boundaries.

**Acceptance criteria.** Round-trip tests at every precision level, and across
date boundaries. An unknown offset is never turned into UTC.

**Interim in pimio.** None. pimio does no date math on raw tags or canonical
values; the batch shift waits for this request.

## U2. Typed read-only technical fields

**Priority:** Important.

**pimio use case.** Camera and lens filters, "taken with" display, correct
orientation, and grid sizing before decoding. These must work for photos and
alongside the video equivalents.

**Current libumm behavior.** Make, model, lens, EXIF orientation, and pixel
dimensions are intentionally *not* canonical (libumm rule C17: no EXIF domain).
They appear only as `BaseEntry` strings in `dumpAll()` / `dumpUnmapped()`
(`metadata.hpp:33-42`), so a caller would have to know the tag keys and parse
the values. That is exactly the tag knowledge pimio must not hold.

**Requested.** A separate, read-only *technical* accessor family that
respects C17 (not canonical and not writable), with typed values and
documented sources. For example: `cameraMake()`, `cameraModel()`,
`lensModel()`, `orientation()` (EXIF 1–8 or video rotation in degrees),
`pixelSize()`. These pair with the video technical properties in U3, so the
same accessors work for both media types.

**Acceptance criteria.** Typed values for JPEG, HEIC, the main RAW formats,
and MP4/MOV. A defined absent state. The source key is reported for each value.

**Interim in pimio.** Show the values in the read-only view only. No camera
filters, orientation, or pixel size taken from metadata until delivered.

## U3. Video technical properties without ExifTool

**Priority:** Important.

**pimio use case.** Duration and frame size in the browser and the inspector
for plain MP4/MOV files, which often have no XMP, even when ExifTool is not
installed.

**Current libumm behavior.** The canonical video properties exist
(`iptc.video.fileDuration`, `frameSize`, audio properties). libumm's own
analysis lists the QuickTime-to-technical cast (`Duration`, `ImageWidth`,
`ImageHeight`, `VideoFrameRate`, `Rotation`, audio) as **later**
(`docs/analysis/2026-10-03-casting-and-canonical-model-decisions.md:462`).
Whether Exiv2 alone fills them for MP4/MOV without XMP is to be confirmed in
the M0 spike.

**Requested.** Fill the canonical video technical properties from QuickTime
atoms, and document which backend is required for each one.

**Acceptance criteria.** The MP4 and MOV fixtures without XMP report duration,
frame size, and rotation, together with the backend requirement.

**Interim in pimio.** Show "unknown", with the libumm reason. The thumbnail
pipeline's video decoder is not a metadata source.

## U11. Durable write option

**Priority:** Nice to have. pimio's acknowledgement point for managed
originals is the LORE commit, so v1 is not blocked.

**pimio use case.** Crash safety and flush-before-acknowledge apply to every
save. Any write pimio acknowledges outside a LORE commit (for example in-place
edits of referenced files, if they are ever approved) must be durable when
`umm::write` returns.

**Current libumm behavior.** `src/core/atomic_write.cpp` copies to a temp
file, closes it, and renames it over the original. On POSIX it calls
`std::filesystem::rename` without `fsync` on the temp file or the parent
directory. On Windows, `MoveFileExW(MOVEFILE_WRITE_THROUGH)` is used only on
the fallback path, not on the primary `ReplaceFileW` path.

**Requested.** `WriteOptions::durable` (default off, to keep current speed):
flush the temp file, replace, and flush the parent directory on POSIX; flush
and use write-through on Windows.

**Acceptance criteria.** A documented durability guarantee per platform. The
fault-injection tests cover the new flush steps.

**Interim in pimio.** Managed originals: readback plus LORE commit, as today.
No in-place writes to referenced originals.

## U12. Legacy and hierarchical keyword sources

**Priority:** Important for importing existing libraries.

**pimio use case.** Users arrive from Picasa, Windows Photos, Lightroom, and
digiKam. Their keywords and titles often live in Windows `XP*` tags, PNG text
chunks, QuickTime user data, or Lightroom's `lr:hierarchicalSubject`. pimio
shows these read-only today, but they do not appear as canonical keywords,
titles, or descriptions, so search and filtering miss them.

**Current libumm behavior.** libumm's analysis lists these casts as **later**
(`docs/analysis/2026-10-03-casting-and-canonical-model-decisions.md:464-468`):
`windowsXP`, `exifUserComment`, `pngText`, `quickTimeUserData`, and
`hierarchicalKeywords` (rule H6, leaf term only).

**Requested.** Prioritize these casts as *read* sources, with
`report_casts` showing their provenance. Whether pimio's later writes remove or
update the legacy copy is libumm's write-sync decision; document it.

**Acceptance criteria.** Fixtures written by each source tool produce the
expected canonical values, and the casts are listed in `castCandidates()`.

**Interim in pimio.** Read-only view only. No pimio-side parsing of these tags.

## U13. People regions, cross-media

**Priority:** Nice to have (post-v1; face features are deferred in
[pimio-v1-implementation.md](pimio-v1-implementation.md)).

**pimio use case.** Optional local face tagging stores regions and confirmed
names "in XMP-compatible regions where appropriate"
([modern-picasa.md](modern-picasa.md)).

**Current libumm behavior.** `iptc.photo.imageRegion` exists and
`personShown` is cross-media. libumm's analysis lists the
`regionsPeople` cast (MWG `mwg-rs:Regions`, Microsoft `MPRI`) as **later**
(`casting-and-canonical-model-decisions.md:469`). There is no video region
equivalent.

**Requested.** The `regionsPeople` read cast. Guidance on writing person
regions through `imageRegion`, including whether MWG regions are kept in sync.
A position on video regions, even if that position is "not planned".

**Interim in pimio.** None. The face feature waits for this.

## U14. Payload fingerprint excluding metadata

**Priority:** Nice to have.

**pimio use case.** Duplicate detection, and recognizing that a file edited
in another tool (metadata only) is still the same photo. A whole-file hash
changes on every metadata edit.

**Current libumm behavior.** There is no API to hash or locate the media
payload separately from metadata segments. Knowing where the segments are is
container knowledge, which pimio must not reimplement.

**Requested.** `payloadDigest(path)`: a stable digest of the media essence
that excludes the metadata carriers libumm writes, documented per type, or
`unsupported` where it cannot be computed.

**Acceptance criteria.** The digest is unchanged by any `umm::write` and
changes when pixel or sample data changes.

**Interim in pimio.** Whole-file fingerprints, as today.

## U5. Read from bytes or a stream

**Priority:** Nice to have. libumm reads by path, and pimio tests above the
adapter use fakes. Filed only if a real need appears, for example reading
from LORE without a checkout.

## U6. Canonical mappings for more types

**Priority:** Nice to have. BMP, MKV, AVI, WAV, and ASF have no canonical
mapping in v0.1.2, so pimio shows their metadata read-only. File when users
need to edit these types.

## U7. Application packaging and ExifTool configuration guidance

**Priority:** Important for M7.

**Requested.**

- Guidance for GUI applications that link libumm: which notices to ship, how
  to provide the corresponding source, and the recommended way to locate
  ExifTool.
- The thread-safety and call-order rules of
  `BackendManager::configureExifTool` (`backend.hpp:129`): may it be called
  more than once, or after reads have started (for example when the user
  changes the ExifTool path in settings)?

**Interim in pimio.** Configure ExifTool once, at startup, on the metadata
worker. Changing the path requires a restart. Follow the umm CLI's packaging.

## Considered and not requested

- **GPS track geotagging.** Already in libumm (`importTrack`, `matchTrack`,
  write-back through `locationCreated` / `locationShot`).
- **Write previews.** `WriteOptions::dry_run` and `WriteReport::written`
  already list the keys a save would touch.
- **Embedded preview/thumbnail extraction.** This is a decode concern, not
  metadata. pimio's thumbnail pipeline owns it. Revisit only if decoding
  embedded RAW previews needs container knowledge that only libumm has.
- **Timed GPS tracks inside videos.** Out of scope for v1. Reconsider with
  U13 when video location editing beyond a single point is planned.
