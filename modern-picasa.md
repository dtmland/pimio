# A Modern, Native Picasa

This document maps each capability described in [picasa.md](picasa.md) to a
current implementation approach for a Windows, macOS, and Linux application.
It recommends shipping native builds for all three platforms, not a Windows
application wrapped in Wine.

## Review of the Existing Notes

The existing document is a useful inventory of product and architectural ideas,
but its historical implementation details should be treated as reverse-
engineering notes rather than an authoritative Picasa specification: it has no
primary-source citations. In particular, the custom database formats and
platform-porting details should be verified before they are relied on as
historical facts.

The durable ideas are more important than reproducing the old implementation:
responsive GPU browsing, incremental indexing, recoverable metadata,
non-destructive editing, and background work that never blocks interaction.

## Recommended Foundation

Use **Qt 6 with C++ and QML** for the desktop application. It has mature,
native integrations for the three target platforms, accessible controls, file
dialogs, drag and drop, menus, and a rendering abstraction that selects
Direct3D on Windows, Metal on macOS, and Vulkan or OpenGL on Linux. License
compliance for an LGPL distribution requires dynamic linking; review the
license of each selected Qt module.

| Concern | Recommended building block | Why |
| --- | --- | --- |
| Native UI and GPU canvas | [Qt 6](https://www.qt.io/) QML Scene Graph | One native codebase with platform-appropriate rendering and interaction. |
| Image load, resize, and tiles | [libvips](https://www.libvips.org/) | Fast, low-memory, lazy image pipelines and Deep Zoom-style pyramids. |
| Library cache and search | [SQLite](https://sqlite.org/) with [FTS5](https://sqlite.org/fts5.html) | ACID local database, simple backup, and embedded full-text search. |
| EXIF, IPTC, XMP | [Exiv2](https://exiv2.org/) and [ExifTool](https://exiftool.org/) for compatibility testing/import | Standards-based metadata rather than an undocumented app-specific format. Review Exiv2/ExifTool licensing before embedding or redistributing. |
| Camera RAW | [LibRaw](https://www.libraw.org/) | Broad camera support and access to embedded previews. |
| Image codecs | [libjpeg-turbo](https://libjpeg-turbo.org/), [libheif](https://github.com/strukturag/libheif), and [libavif](https://github.com/AOMediaCodec/libavif) | Fast JPEG transforms plus common modern import formats. |
| Video | [FFmpeg](https://ffmpeg.org/) and [libmpv](https://mpv.io/) | Cross-platform demuxing, thumbnails, playback, and hardware-decoding fallbacks. |
| File monitoring | [Qt QFileSystemWatcher](https://doc.qt.io/qt-6/qfilesystemwatcher.html) supplemented by platform-native recursive watching where needed | A portable starting point; production-scale recursive libraries need overflow detection and periodic reconciliation. |

Keep platform-specific code behind small adapters—for native notifications,
media hardware acceleration, sandbox permissions, and installers—rather than
lowering the whole application to the least common denominator.

## Wine Is Not the Portability Strategy

Wine was a practical compatibility layer for a Windows-era Picasa, but it is
not the right primary architecture now. A Wine-first application complicates
macOS support, filesystem event reliability, HiDPI behavior, GPU drivers,
hardware video decoding, and modern webview or AI runtimes. It also prevents
the application from behaving like a normal Mac or Linux program.

Native Qt builds provide the same shared-code benefit while using each
platform's native APIs. Wine can remain an unsupported migration path for
people running an older Windows-only tool; it should not be required or tested
as a supported runtime for this application.

## Mapping the Original Architecture to a Modern Design

### 1. Rendering, Browsing, and Platform UI

Build the photo grid, timeline, map, and editor preview as QML views over a
virtualized model. Only request thumbnails and GPU textures for visible items
plus a small prefetch margin. Use the Qt rendering abstraction rather than
targeting legacy OpenGL directly; this permits native Metal, Direct3D, Vulkan,
or OpenGL selection without maintaining separate renderers.

For effects that exceed built-in QML capabilities, implement a small,
well-tested shader layer with Qt Shader Tools. Retain an OpenGL fallback only
where a target system requires it, rather than making it the primary graphics
API. The UI shell, menus, and drag-and-drop should be native Qt features, not
a Wine or browser wrapper.

### 2. Library Database, Metadata, Thumbnails, and Watching

#### Replace `.pmp` Columns with a Versioned SQLite Cache

Use SQLite as the local derived index: media identity and paths, file
fingerprints, extracted metadata, collection membership, thumbnail locations,
job state, and search indexes. Add schema migrations and transactional updates.
SQLite is easier to inspect, repair, query, and evolve than a custom column
format, while preserving the low-latency queries Picasa needed.

The cache must be rebuildable. Store a stable file identity plus a content
fingerprint so moved or renamed items can be recognized without treating them
as new photos.

#### Replace `.picasa.ini` with Interoperable Sidecars

Write portable metadata to embedded EXIF/IPTC/XMP where the format safely
supports it. Use adjacent `.xmp` sidecars for RAW files and for edits that
should not mutate originals. Store application-only state, such as UI layout,
thumbnail cache references, and incomplete jobs, in SQLite.

This produces the useful Picasa property of recoverability without inventing a
new text format: a new installation can re-index the folders and recover
captions, ratings, tags, regions, and edit recipes from standard metadata.
Detect and surface sidecar conflicts instead of silently choosing a winner.

#### Thumbnail Cache and Image Pyramids

Generate thumbnails and previews with libvips, preferring decode-time shrinking
and embedded RAW previews. Keep cache entries in a versioned cache directory
keyed by content fingerprint, requested size, color-management version, and
edit-recipe hash. SQLite indexes those files and supports cache eviction.

This is safer than one monolithic thumbnail blob: individual entries can be
atomically replaced, corruption is isolated, and operating-system cache tools
work naturally. Generate additional pyramid tiles only for unusually large
images or deep zoom, using libvips `dzsave`-style output. Use AVIF or WebP for
new cache artifacts after measuring decode latency; retain JPEG as a broadly
compatible fallback. HEIC import is useful, but HEVC patent obligations make
HEIC cache/output encoding a legal review item.

#### Incremental Folder Watching

Watch configured library roots and coalesce duplicate create, rename, write,
and delete events into a durable SQLite job queue. Each job re-scans only its
affected directory or file. Watchers are hints, not truth: record event
overflows, network-volume limitations, and missed roots, then schedule a
low-priority reconciliation scan. This preserves fast updates without trusting
any platform's notification stream completely.

### 3. Image Pipeline

Use an explicit image-request pipeline: embedded preview or thumbnail first,
then a color-managed medium preview, then the full-resolution source only when
needed. Cancel obsolete requests as the viewport changes. Decode RAW files
with LibRaw and use their embedded JPEG previews for library browsing; run full
demosaicing in a background worker only for editing and export.

Perform JPEG rotation and flip with libjpeg-turbo's `jpegtran` lossless
transforms when the image's MCU boundaries allow it. Otherwise make the
tradeoff explicit: update orientation metadata for an application view, or
perform a documented re-encode when the user requests an exported, physically
rotated file.

Keep a non-destructive edit recipe in XMP where possible and in a
versioned application namespace when necessary. The recipe is an ordered,
parameterized list of operations—crop, orientation, exposure, color,
straightening, and lens correction—not a serialized shader command string.
Render previews through libvips and GPU shaders; replay the same recipe at
full resolution for export. [GEGL](https://gegl.org/) and
[Lensfun](https://lensfun.github.io/) are useful optional components for
more advanced filters and lens correction, but the initial organization-first
product should expose only a small, dependable edit set.

### 4. Video Pipeline

Do not depend on installed system codecs. Bundle or dynamically link an
appropriately licensed FFmpeg build for demuxing, decode, stream-copy export,
and thumbnail extraction. Embed libmpv for playback so the application gets
cross-platform hardware-acceleration selection, audio sync, subtitles, and a
software-decoding fallback.

Create video thumbnails in background jobs by seeking to a useful early
keyframe and rejecting black or near-black frames where practical. Store
duration, rotation, audio presence, codecs, and capture timestamps in SQLite,
and render a duration/play overlay in the same grid model as images.

Store non-destructive trim in the edit recipe as in/out timestamps. Offer
lossless export only when the requested boundaries are compatible with source
keyframes and containers; otherwise clearly label a re-encoded export.
[LosslessCut](https://github.com/mifi/lossless-cut) is a valuable GPL-licensed
reference and optional external handoff, rather than an embedded dependency
for a differently licensed application.

### 5. Intelligence, Sync, Effects, and Search

#### Local Face Detection

Make face analysis optional, local by default, pausable, and removable. Start
with [MediaPipe](https://ai.google.dev/edge/mediapipe)'s Apache-licensed face
detector to find face regions. Add identity clustering only after choosing
models whose *weights* permit the intended distribution; some popular
InsightFace weights are non-commercial despite permissive code licenses.

Persist face regions and user-approved names locally and in XMP-compatible
regions where appropriate. Never require cloud upload to find faces. Run
analysis at low priority, with thermal/battery limits and an explicit option to
delete its derived embeddings.

#### Optional Sync

Treat sync as an opt-in product feature, not part of indexing. Synchronize
originals and `.xmp` sidecars with a conflict-aware protocol, and rebuild
derived caches locally. [Syncthing](https://syncthing.net/) is a useful
device-to-device option; [rclone](https://rclone.org/) is useful for cloud
backup targets. Neither should silently merge incompatible metadata changes:
keep both versions and give the user a resolution view.

#### Responsive Effects and Background Work

Use separate priority classes: visible-thumbnail and interaction work first;
viewer decode and export next; scanning, face analysis, and sync last. Use a
bounded CPU worker pool, bounded decode memory, cancellation tokens, and a
persistent SQLite job queue. This is the modern equivalent of Picasa's
responsive thread pool and continues safely after a restart.

Preview edits by applying the recipe to a reduced image on the GPU or through
libvips. Debounce sliders and cancel obsolete previews. Never alter the
original until the user explicitly exports or requests a supported metadata
write.

#### Search

Index filenames, folder names, tags, captions, people, camera fields, dates,
and locations with SQLite FTS5. Its trigram tokenizer supports fast
partial-match search; ordinary indexed columns cover date, rating, type,
camera, and location filters. Update the model incrementally as a query is
typed, while preserving the virtualized grid. For substantially richer text
or future semantic search at very large scale, evaluate
[Tantivy](https://github.com/quickwit-oss/tantivy) as a separate embedded
search index, not as a day-one dependency.

### 6. Resilience and Product Boundaries

Use write-ahead logging and transactionally record every change to the derived
index and job queue. Make all generated cache data disposable. Back up edit
recipes and portable metadata with the media library, and provide a library
health screen that reports unavailable roots, watcher overflows, cache size,
and sidecar conflicts.

The first release should prioritize the goals in [pimio.md](pimio.md):
timeline organization, metadata repair, images and videos, portable metadata,
and a fluid library. Cloud accounts, advanced AI identification, complex
darkroom editing, and a custom distributed file store can be added only after
the local library is robust and recoverable.

## Delivery Sequence

1. Establish the native Qt shell, SQLite schema, import scanner, metadata
   extraction, and virtualized chronological image/video browser.
2. Add persistent thumbnails, resilient watching, search, portable XMP edits,
   rotation/crop, and reliable export.
3. Add libmpv playback, video thumbnails, non-destructive trimming, maps, and
   timestamp-repair workflows.
4. Add opt-in local face detection and clustering, then separately evaluate
   sync providers and advanced editing features.

At each stage, test Windows, current macOS, and a representative X11/Wayland
Linux environment natively. Test large libraries, disconnected network drives,
filesystem event loss, unsupported media, low disk space, cancellation, and
sidecar conflicts as product behavior—not as platform-specific afterthoughts.
