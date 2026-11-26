# Feature requests for libumm

Requests for [libumm](https://github.com/dtmland/libumm), written for the
libumm maintainers. Each item states a caller need, the behavior observed in
the **v0.1.2** source tag, the requested API, and acceptance criteria. Header
citations are `include/umm/<file>:<line>` in that tag unless noted. Recheck
against the current sources before implementing.

These requests come from a desktop application that organizes large local
photo and video libraries.

## What the caller needs from metadata

- **Large libraries, kept fresh.** Scan and watch tens of thousands of files
  on background workers. The work must be cancellable and must not block the
  UI.
- **Time is the primary axis.** Capture time drives ordering, and users must
  be able to repair wrong camera clocks without losing precision or inventing
  time zones.
- **Photos and videos behave alike.** One editor, one set of fields, and the
  same filters for both media types.
- **Location and people** in standard, portable fields that other tools can
  read.
- **Honest UI.** Controls are disabled with a reason, never silently ignored.
- **Crash safety.** A save is not acknowledged until it is durable.

## Priority key

| Priority | Meaning |
| --- | --- |
| **Blocking** | A core workflow cannot work as intended without it, or must ship with a recorded degradation. |
| **Important** | The workflow works, but with a visible cost (speed, UX, or a feature gap). |
| **Nice to have** | Later, or convenience. |

## Summary

| # | Request | Priority | Verified against v0.1.2 | Caller impact |
| --- | --- | --- | --- | --- |
| [U1](#u1-concurrent-reads--thread-safety-contract) | Concurrent reads / thread-safety contract | **Blocking** for scan throughput | Confirmed gap | Parallel scan and watch |
| [U9](#u9-cancellation-and-deadlines-per-call) | Cancellation and deadlines per call | Important | Confirmed gap | Cancel and shutdown |
| [U10](#u10-selective-and-batch-reads-for-scanning) | Selective and batch reads for scanning | Important | Confirmed gap | First-scan cost |
| [U4](#u4-per-property-capability-with-a-reason) | Per-property capability with a reason | **Blocking** for editor gating | Confirmed gap | Enable or disable controls |
| [U8](#u8-datetime-shift-helpers) | `DateTime` shift helpers | Important | Confirmed gap | Batch clock repair |
| [U2](#u2-typed-read-only-technical-fields) | Typed read-only technical fields (photo) | Important | Confirmed gap (by design, C17) | Camera filters, orientation, sizing |
| [U3](#u3-video-technical-properties-without-exiftool) | Video technical properties without ExifTool | Important | Deferred upstream ("later") | Duration and frame size |
| [U11](#u11-durable-write-option) | Durable write option (fsync) | Nice to have | Confirmed gap | Crash-safe in-place writes |
| [U12](#u12-legacy-and-hierarchical-keyword-sources) | Legacy and hierarchical keyword/title sources | Important | Deferred upstream ("later") | Import of existing libraries |
| [U13](#u13-people-regions-cross-media) | People regions, cross-media | Nice to have | Deferred upstream ("later") | Face tagging |
| [U14](#u14-payload-fingerprint-excluding-metadata) | Payload fingerprint excluding metadata | Nice to have | Confirmed gap | Duplicate detection |
| [U5](#u5-read-from-bytes-or-a-stream) | Read from bytes or a stream | Nice to have | Confirmed gap | Non-path reads |
| [U6](#u6-canonical-mappings-for-more-types) | Canonical mappings for BMP, MKV, AVI, WAV, ASF | Nice to have | Confirmed gap | Editing those types |
| [U7](#u7-application-packaging-and-exiftool-configuration-guidance) | Packaging and ExifTool configuration guidance | Important | Partly documented | Shipping a GUI that links libumm |

Already in libumm, so **not** requested: GPS track import and capture-time
matching (`importTrack`, `matchTrack` in `track.hpp`), cross-media `rating()`
and `personShown`, atomic temp-plus-rename writes, `WriteOptions::dry_run`
with a `WriteReport::written` key list, `ReadOptions::report_casts`,
`OffsetTimeOriginal` folded into the canonical `dateCreated` offset, and
`dumpAll()` / `dumpUnmapped()` for the read-only view.

---

## U1. Concurrent reads / thread-safety contract

**Priority:** Blocking for scan throughput. Callers can serialize every read
on one worker, but large-library scans will then be slower than they need to
be.

**Use case.** Scan, watch, and background jobs read metadata for many files
at once on a bounded worker pool. If every metadata read must share one
worker, extraction becomes the scan bottleneck, and a single slow ExifTool
read (a large video) stalls every other read.

**Current behavior (v0.1.2).**

- `include/umm/backend.hpp:11-13`: "each Backend instance is single-threaded.
  Callers must not share an instance across threads. BackendManager may pool
  instances later; this session's manager holds one instance per registered
  id."
- `BackendManager::instance()` (`backend.hpp:126`) is a process singleton that
  `umm::read` / `umm::write` use internally. Callers cannot choose an
  instance, so two threads calling `umm::read` share the same Exiv2 adapter
  and the same ExifTool `-stay_open` process.
- No locking appears in the public headers, and the thread-safety of the free
  functions (`read`, `write`, `capabilities`, `describe`, `cast`) is not
  documented.

**Requested.** One of these, documented in the public headers:

1. **Preferred:** `BackendManager` pools backend instances, with a
   configurable pool size (one ExifTool process per pooled instance), and the
   free functions become safe to call from multiple threads.
2. **Minimum:** the free functions serialize internally (they are documented
   as thread-safe, but run one at a time). Callers can then drop their own
   serialization, and pooling becomes a later improvement.
3. **Alternative:** an explicit `Session`/`Context` object that owns its own
   backend instances, for which `read`/`write` overloads exist. One per
   worker thread.

Also state which functions need no backend at all (for example `describe`,
`cast`, and DateTime helpers) and are therefore always thread-safe.

**Acceptance criteria.**

- Thread-safety is stated for every public entry point.
- A libumm test reads N fixtures from M threads in parallel (under TSan in
  CI) and gets results identical to serial reads, for both the Exiv2 and the
  ExifTool backends.
- Pooled ExifTool processes are bounded, are reaped on shutdown, and respect
  `command_timeout`.

## U9. Cancellation and deadlines per call

**Priority:** Important.

**Use case.** Viewport changes, library close, and application shutdown must
cancel queued and running metadata work quickly. With one serialized worker
(U1), a long ExifTool read on a large video delays everything behind it,
including shutdown.

**Current behavior.** `ReadOptions` and `WriteOptions` (`umm.hpp:39-58`) have
no cancellation token or deadline. The only bound is the process-wide
`ExifToolConfig::command_timeout` (30 s default, `backend.hpp:121`), which
kills and restarts the ExifTool child.

**Requested.** An optional per-call cancellation token (a callable or atomic
flag) and an optional deadline in `ReadOptions`, `WriteOptions`, and
`SyncOptions`. A cancelled call returns a distinct `ErrorCode` (for example
`cancelled`). A cancelled write must leave the original untouched, which the
existing atomic temp-plus-rename write already makes possible.

**Acceptance criteria.** Cancelling an in-flight ExifTool read returns within
a bounded time, does not leave a zombie process, and the next call succeeds.
Cancelling a write leaves the original file byte-identical.

## U10. Selective and batch reads for scanning

**Priority:** Important.

**Use case.** A first scan of a large library needs only a few properties per
file to build the timeline: `dateCreated`, location, rating, and (once U2
lands) orientation and pixel size. The full canonical read, unmapped dump,
and sidecar pairing can wait until the user opens the inspector.

**Current behavior.** `umm::read` (`umm.hpp`) always performs a full read:
every canonical property, `dumpAll()`, and sidecar merging when
`merge_sidecar` is true. No property filter or multi-file entry point exists.

**Requested.**

- `ReadOptions::properties`: an optional allow-list of canonical ids. When
  set, libumm may skip work that is not needed for them, including the
  unmapped dump.
- An optional `readMany(paths, options, callback)` that streams one result
  per path, so the ExifTool backend can batch its requests.

**Acceptance criteria.** A benchmark in libumm shows that a selective read is
measurably faster on JPEG/HEIC/MP4 fixtures. Results for the requested ids
are identical to a full read.

## U4. Per-property capability with a reason

**Priority:** Blocking for editor gating.

**Use case.** The editor shows the canonical properties for the selected
items, which may mix photos and videos. Each control must be enabled only
when that property can be written to every selected file with the
embedded-only policy and the backends available now (for example, when
ExifTool is missing). When a control is disabled, the UI must show why.
Callers should not reconstruct per-property access from category tables;
that duplicates libumm's mapping knowledge.

**Current behavior.** `capabilities()` (`capabilities.hpp:62`) reports per
backend and per *category* (`exif`, `iptc_iim`, `xmp`, `icc`, `thumbnail`)
plus `LocationAccess`, `identify_only`, free-text `notes`,
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
what `write(..., dry_run = true)` would do. The reason codes are documented
and stable within a minor version.

## U8. DateTime shift helpers

**Priority:** Important. Batch time shift is a core organizer feature.

**Use case.** "Camera clock was 1 h 12 min fast", or "these were taken in
UTC+2". The application applies one shift to many items at once, photos and
videos alike, and must not invent a time zone when the file has none.

**Current behavior.** `umm::DateTime` (`value.hpp:30`) carries partial
precision (year required; month through sub-second optional) and an optional
UTC offset, where absent means unknown, not UTC (H12). There are no
arithmetic, shift, or offset-change helpers.

**Requested.**

- `shift(DateTime, std::chrono::seconds)`, which keeps the existing
  precision. Define the behavior when the value has no time component, for
  example by rejecting the shift or requiring day granularity.
- `withOffset(DateTime, minutes, mode)` with two modes: *reinterpret* (same
  wall time, new zone) and *convert* (same instant, new wall time).
  Converting must fail when the offset is unknown.
- Correct handling of calendar rollover across month, year, and leap-day
  boundaries.

**Acceptance criteria.** Round-trip tests at every precision level, and
across date boundaries. An unknown offset is never turned into UTC.

## U2. Typed read-only technical fields

**Priority:** Important.

**Use case.** Camera and lens filters, a "taken with" display, correct
orientation, and grid sizing before decoding. These must work for photos and
alongside the video equivalents in U3.

**Current behavior.** Make, model, lens, EXIF orientation, and pixel
dimensions are intentionally *not* canonical (libumm rule C17: no EXIF
domain). They appear only as `BaseEntry` strings in `dumpAll()` /
`dumpUnmapped()` (`metadata.hpp:33-42`), so a caller would have to know the
tag keys and parse the values. That is tag knowledge callers should not
hold.

**Requested.** A separate, read-only *technical* accessor family that
respects C17 (not canonical and not writable), with typed values and
documented sources. For example: `cameraMake()`, `cameraModel()`,
`lensModel()`, `orientation()` (EXIF 1–8 or video rotation in degrees),
`pixelSize()`. These pair with the video technical properties in U3, so the
same accessors work for both media types.

**Acceptance criteria.** Typed values for JPEG, HEIC, the main RAW formats,
and MP4/MOV. A defined absent state. The source key is reported for each
value.

## U3. Video technical properties without ExifTool

**Priority:** Important.

**Use case.** Duration and frame size in the browser and the inspector for
plain MP4/MOV files, which often have no XMP, even when ExifTool is not
installed. A separate video decoder should not be required as a metadata
source.

**Current behavior.** The canonical video properties exist
(`iptc.video.fileDuration`, `frameSize`, audio properties). libumm's own
analysis lists the QuickTime-to-technical cast (`Duration`, `ImageWidth`,
`ImageHeight`, `VideoFrameRate`, `Rotation`, audio) as **later**
(`docs/analysis/2026-10-03-casting-and-canonical-model-decisions.md:462`).
Whether Exiv2 alone fills them for MP4/MOV without XMP is not established.

**Requested.** Fill the canonical video technical properties from QuickTime
atoms, and document which backend is required for each one.

**Acceptance criteria.** The MP4 and MOV fixtures without XMP report
duration, frame size, and rotation, together with the backend requirement.

## U11. Durable write option

**Priority:** Nice to have. Callers that acknowledge a save only after a
later content-store commit are not blocked. Callers that treat `umm::write`
itself as the acknowledgement point are.

**Use case.** Crash safety and flush-before-acknowledge apply to every save
the application treats as complete. An in-place edit of a file the user still
owns outside a content-addressed store must be durable when `umm::write`
returns.

**Current behavior.** `src/core/atomic_write.cpp` copies to a temp file,
closes it, and renames it over the original. On POSIX it calls
`std::filesystem::rename` without `fsync` on the temp file or the parent
directory. On Windows, `MoveFileExW(MOVEFILE_WRITE_THROUGH)` is used only on
the fallback path, not on the primary `ReplaceFileW` path.

**Requested.** `WriteOptions::durable` (default off, to keep current speed):
flush the temp file, replace, and flush the parent directory on POSIX; flush
and use write-through on Windows.

**Acceptance criteria.** A documented durability guarantee per platform. The
fault-injection tests cover the new flush steps.

## U12. Legacy and hierarchical keyword sources

**Priority:** Important for importing existing libraries.

**Use case.** Users arrive from Picasa, Windows Photos, Lightroom, and
digiKam. Their keywords and titles often live in Windows `XP*` tags, PNG text
chunks, QuickTime user data, or Lightroom's `lr:hierarchicalSubject`. Those
values can be shown from the read-only unmapped view, but they do not appear
as canonical keywords, titles, or descriptions, so search and filtering miss
them.

**Current behavior.** libumm's analysis lists these casts as **later**
(`docs/analysis/2026-10-03-casting-and-canonical-model-decisions.md:464-468`):
`windowsXP`, `exifUserComment`, `pngText`, `quickTimeUserData`, and
`hierarchicalKeywords` (rule H6, leaf term only).

**Requested.** Prioritize these casts as *read* sources, with `report_casts`
showing their provenance. Whether a later canonical write removes or updates
the legacy copy is libumm's write-sync decision; document it.

**Acceptance criteria.** Fixtures written by each source tool produce the
expected canonical values, and the casts are listed in `castCandidates()`.

## U13. People regions, cross-media

**Priority:** Nice to have. Not required for an initial library release.
Face tagging can wait for this.

**Use case.** Optional local face tagging stores regions and confirmed names
in XMP-compatible regions so other tools can read them.

**Current behavior.** `iptc.photo.imageRegion` exists and `personShown` is
cross-media. libumm's analysis lists the `regionsPeople` cast (MWG
`mwg-rs:Regions`, Microsoft `MPRI`) as **later**
(`casting-and-canonical-model-decisions.md:469`). There is no video region
equivalent.

**Requested.** The `regionsPeople` read cast. Guidance on writing person
regions through `imageRegion`, including whether MWG regions are kept in
sync. A position on video regions, even if that position is "not planned".

## U14. Payload fingerprint excluding metadata

**Priority:** Nice to have.

**Use case.** Duplicate detection, and recognizing that a file edited in
another tool (metadata only) is still the same photo or video. A whole-file
hash changes on every metadata edit. Knowing where the metadata segments are
is container knowledge that callers should not reimplement.

**Current behavior.** There is no API to hash or locate the media payload
separately from metadata segments.

**Requested.** `payloadDigest(path)`: a stable digest of the media essence
that excludes the metadata carriers libumm writes, documented per type, or
`unsupported` where it cannot be computed.

**Acceptance criteria.** The digest is unchanged by any `umm::write` and
changes when pixel or sample data changes.

## U5. Read from bytes or a stream

**Priority:** Nice to have. libumm reads by path. A byte or stream entry
point would let callers and tests read metadata without first writing a
filesystem path, for example from an in-memory buffer or from a content
store that has not checked the file out to disk. Not required for path-based
use.

## U6. Canonical mappings for more types

**Priority:** Nice to have. BMP, MKV, AVI, WAV, and ASF have no canonical
mapping in v0.1.2, so callers can show their metadata only through the
read-only unmapped view. Add mappings when users need to edit these types.

## U7. Application packaging and ExifTool configuration guidance

**Priority:** Important for shipping a GUI that links libumm.

**Requested.**

- Guidance for GUI applications that link libumm: which notices to ship, how
  to provide the corresponding source, and the recommended way to locate
  ExifTool.
- The thread-safety and call-order rules of
  `BackendManager::configureExifTool` (`backend.hpp:129`): may it be called
  more than once, or after reads have started (for example when the user
  changes the ExifTool path in settings)?

## Considered and not requested

- **GPS track geotagging.** Already in libumm (`importTrack`, `matchTrack`,
  write-back through `locationCreated` / `locationShot`).
- **Write previews.** `WriteOptions::dry_run` and `WriteReport::written`
  already list the keys a save would touch.
- **Embedded preview/thumbnail extraction.** This is a decode concern, not
  metadata. Revisit only if decoding embedded RAW previews needs container
  knowledge that only libumm has.
- **Timed GPS tracks inside videos.** Not required for an initial release.
  Reconsider with U13 when video location editing beyond a single point is
  planned.
