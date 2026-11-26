# libumm Upstream Requests

pimio does not reimplement metadata behavior
([0008](../decisions/0008-libumm-metadata-engine.md) §1). When pimio needs
something libumm does not provide, it is recorded here, filed with
[dtmland/libumm](https://github.com/dtmland/libumm), and adopted by bumping
the libumm pin in `cmake/PimioUmm.cmake`. Workarounds are allowed only with
maintainer approval and a removal condition recorded here.

Pinned libumm version: **0.1.2** (planned; see
[libumm-migration.md](libumm-migration.md) M1).

The text to send upstream — use case, evidence from the libumm source,
proposed API, and acceptance criteria, with no pimio references — is in
[libumm-feature-requests-upstream.md](libumm-feature-requests-upstream.md).
The internal copy, including interim notes, is in
[libumm-feature-requests.md](libumm-feature-requests.md). This table is the
status tracker.

| # | Need | Blocks (pimio) | Status | Upstream | Interim in pimio |
| --- | --- | --- | --- | --- | --- |
| U1 | **Concurrent reads.** `Backend` instances are single-threaded and shared through `BackendManager`. pimio needs a documented safe way to read metadata from several threads, such as per-thread backend instances or an internally serialized API. | Scan/watch throughput (M3) | Confirmed in v0.1.2 (`backend.hpp:11-13`); **blocking** for throughput; to file | — | Serialize all libumm calls through one metadata worker |
| U2 | **Typed technical fields.** Photo make/model/lens, EXIF orientation, and pixel dimensions are intentionally non-canonical (C17). pimio needs them as typed values (string/int) for camera filters, orientation, and sizing, not only as dump strings. | Projection camera filters, sizing (M3/M4) | To file | — | Read the values as libumm's read-only entries report them; no pimio tag parsing |
| U3 | **Video technical properties without ExifTool.** For plain MP4/MOV without XMP, check whether duration, frame size, and audio presence reach `iptc.video.fileDuration` / `frameSize` / audio properties, or at least the read-only view, when only Exiv2 is available. | Video duration/size in browser (M3, Inc 10) | Upstream lists the cast as "later"; verify Exiv2-only reads in M0, then file | — | Show "unknown" with libumm's reason |
| U4 | **Capability reporting for pimio's UI.** pimio needs per-path, per-property read/write availability that includes the backend reason, so it can disable controls with an explanation. `capabilities(path)` reports only per backend and category. | Editor gating (M5/M6) | Confirmed gap in v0.1.2 (`capabilities.hpp` is per category only); **blocking** for M6; to file | — | None; required before M6 |
| U5 | **Read from bytes or a stream.** libumm reads by path. pimio's tests inject a `core::FileSystem`. Not blocking: pimio tests above the adapter use fakes. | Nice to have | Optional | — | Adapter tests use real fixture files |
| U6 | **Canonical mappings for currently non-canonical types** (BMP, MKV, AVI, WAV, ASF). Without them, these types stay read-only in pimio. | Editing those types | Track; file when users need it | — | Read-only view |
| U7 | **Application packaging guidance.** Document the recommended way for GUI applications to ship notices and corresponding source, and to locate ExifTool. Includes whether `configureExifTool` is safe to call more than once and from which thread. | M7 packaging | To file | — | Follow umm CLI behavior |
| U8 | **Timestamp shift helpers.** Shift or offset a `DateTime` that keeps partial precision and an unknown offset (H12). libumm v0.1.2 has none. | Inc 9 batch shift | Confirmed gap in v0.1.2 (`value.hpp:30`, no helpers); to file | — | None; no pimio date math on raw tags or canonical values |
| U9 | **Cancellation and deadlines per call.** `ReadOptions`/`WriteOptions` have no cancellation token; only the global ExifTool `command_timeout` exists. | Job/scan cancellation, shutdown (M3, M5) | Confirmed gap; to file | — | Cancel between calls only |
| U10 | **Selective and batch reads.** A property allow-list and a multi-file read for fast first scans. | Large-library scan time (M3) | Confirmed gap; measure gain in M0, then file | — | Full serialized reads |
| U11 | **Durable write option.** The atomic write does not fsync the temp file or the directory on POSIX. | In-place writes (not planned for v1) | Confirmed gap; nice to have | — | Readback + LORE commit is the acknowledgement point |
| U12 | **Legacy and hierarchical keyword sources** (Windows XP tags, PNG text, QuickTime user data, `lr:hierarchicalSubject`) cast into canonical keywords/title/description. | Importing existing libraries | Upstream lists as "later"; to file | — | Read-only view |
| U13 | **People regions, cross-media** (MWG/MPRI read cast, region write guidance, video position). | Post-v1 face tagging | Upstream lists as "later"; file before face work | — | None |
| U14 | **Payload fingerprint excluding metadata.** | Duplicate detection across metadata edits | Confirmed gap; nice to have | — | Whole-file fingerprints |

Add new rows as they are found. Never close a row without a libumm release that
pimio has adopted, or a recorded decision that the need was withdrawn.
