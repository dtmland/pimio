# 0008 — libumm is pimio's only metadata engine

Status: **accepted (direction); implementation pending.** The migration is
scheduled in [libumm-migration.md](../plan/libumm-migration.md). Items marked
**Maintainer confirmation** below must be confirmed before the increment that
depends on them starts.

Decision owner: repository maintainer.

Supersedes [0002](0002-metadata-adapter.md) (built-in reader) in full and the
write-adapter choice ("Increment 8 outcome") of
[0007](0007-embedded-metadata-writes.md). The embedded-first and
no-default-sidecar policy of 0007 still applies.

## Decision

All metadata reads and writes in pimio go through
[libumm](https://github.com/dtmland/libumm) (`umm::umm`, C++20). libumm
combines Exiv2 (in-process, fast) and ExifTool (out-of-process, broad) behind
one canonical property model based on the IPTC Photo Metadata Standard and the
IPTC Video Metadata Hub (`iptc.photo.*` and `iptc.video.*`).
pimio keeps no metadata parser, writer, merge logic, or format knowledge of its
own.

### 1. No reinvention

pimio must not reimplement anything libumm does or should do. This covers
container parsing, tag mapping, photo↔video casting, reconciliation and
conflict detection, sidecar merging, storage-policy evaluation, atomic
writes, and backend selection.

If pimio needs a metadata capability that libumm lacks, the developer must:

1. stop and record the gap in
   [libumm-upstream-requests.md](../plan/libumm-upstream-requests.md) with the
   exact API or behavior needed and the pimio feature that is blocked;
2. file it with the libumm project, writing the full request in
   [libumm-feature-requests.md](../plan/libumm-feature-requests.md); and
3. take the libumm release that delivers it by updating the pin.

pimio-side workarounds are not the default. A temporary one is allowed only if
it is listed in the upstream-requests file with its removal condition, and the
maintainer approves it. This mirrors how LORE defects are tracked in
[lore-upstream-issues.md](../plan/lore-upstream-issues.md).

### 2. Canonical properties are first-class, across media

- pimio's metadata domain model is keyed by libumm canonical property ids.
  It uses libumm's typed cross-media accessors (`dateCreated`, `title`,
  `description`, `keywords`, `rating`, `creator`, `locationCreated`, `locationShown`,
  `personShown`, …). It does not use photo-only or video-only field sets.
- Every feature that touches metadata (search, filters, editor, timestamp
  repair, location, export) is defined once in terms of canonical properties.
  It applies to photos and videos equally wherever libumm maps the property in
  both domains. libumm's cross-media accessors and casts make the photo/video
  difference invisible to pimio. Examples: photo `locationCreated` ↔ video
  `locationShot`, with GPS on entry 0; capture time is `dateCreated` in both
  domains.
- **Every edit is a canonical write.** pimio writes only canonical properties,
  through `umm::write`. libumm intentionally does not support creating new
  non-canonical base keys, and pimio does not work around that.
- Canonical conflicts come from libumm (`PropertyValue::resolution`,
  `conflictedPropertyIds`). pimio shows them and lets the user choose a value
  to write back. pimio does not compute conflicts itself.

### 3. Non-canonical metadata is readable, read-only

pimio reads and shows every metadata entry libumm can read, not only canonical
properties. The source is libumm's unmapped/base view (`dumpAll`,
`dumpUnmapped`, `dumpValue`, as `{family, key, value}` entries), and the
values are always read-only.

- Common technical fields that IPTC does not make canonical come from this
  read-only view: photo camera make, model, and lens (`Exif.Image.Make`,
  `Exif.Image.Model`, `Exif.Photo.LensModel`); EXIF orientation; and pixel
  dimensions. libumm deliberately keeps these non-canonical (its C17 rule: no
  EXIF domain). For video, the canonical `iptc.video.recordingDevice`,
  `fileDuration`, `frameSize`, `orientation`, and audio properties are used
  where present.
- pimio may *display* and *index* (search/filter) non-canonical values. It
  must not offer editing for them.
- pimio needs these technical fields as typed values, for example to filter
  by camera or to sort by dimensions. libumm's support for that is a tracked
  upstream request ("typed technical fields"). Until libumm delivers it, pimio
  reads the read-only entries as libumm reports them and does not add its own
  tag parsing.

### 4. Support follows libumm's per-type capability

The libumm supported-types matrix and `umm::capabilities(path)` decide the
level of metadata support for each file. pimio keeps no format list of its
own and adds no per-type restrictions. A file gets everything libumm can do
for it. When libumm improves support for a type, pimio picks it up by
updating the pin, without a pimio code change. The table below describes
libumm v0.1.2 behavior.

| libumm capability for the file | pimio behavior |
| --- | --- |
| Canonical read + write (e.g. JPEG, TIFF, PNG, WebP, GIF*, HEIC*, AVIF*, MP4*, MOV*) | Canonical view and editor; non-canonical entries read-only |
| Canonical read, write needs a backend that is missing (*: ExifTool not found) | Canonical view; editor disabled with a reason; non-canonical entries read-only |
| Canonical read, sidecar-only write (RAW: ARW, CR2, NEF, …) | Canonical view; editor disabled until a RAW write policy is decided (see §5); non-canonical entries read-only |
| No canonical mapping, but readable (BMP, MKV, AVI, WAV, ASF) | Full non-canonical view, read-only. No editing, because libumm writes only canonical properties. If editing is needed for these types, it is an upstream request for canonical mappings. |
| Not readable by libumm | No metadata; the file is still a library item (filename, folder, size, fingerprint) |

\* Needs ExifTool at runtime.

pimio's capture-time ordering always falls back to file-system time when
libumm gives no `dateCreated`.

### 5. Writes are embedded-only

pimio calls `umm::write` with `StoragePolicy::embedded_only`, following 0007.
libumm's write is atomic (temp file plus rename) and returns a `WriteReport`.
pimio keeps its own fingerprint check, readback through `umm::read`, and
one-LORE-revision commit. Those belong to pimio's durable-store contract, not
to metadata handling.

- RAW and other sidecar-default types stay non-editable in v1 unless the
  maintainer approves a sidecar policy under 0007.
- **Maintainer confirmation:** whether pimio reads adjacent `.xmp` sidecars.
  The candidate setting is `ReadOptions::merge_sidecar = true` for referenced
  scans and `false` for managed checkouts. Managed originals in LORE have no
  adjacent sidecar, and pimio never writes one.

### 6. Licensing: binaries are conveyed under GPL-3.0

libumm's source is Apache-2.0 (provisional upstream). Any binary that contains
libumm also contains Exiv2, which is GPL-2.0-or-later. The binary is therefore
conveyed under **GPL-3.0**, whether Exiv2 is linked statically or as a shared
library. libumm does not plan an Exiv2-free build. This replaces the earlier
analysis, which rejected libexiv2 to avoid GPL; that option is no longer
available, so the GPL-3.0 path is accepted.

Consequences for pimio distribution:

- pimio release archives are conveyed under GPL-3.0. They must ship libumm's
  `LICENSE`, `NOTICE.md`, `THIRD-PARTY-NOTICES.md`, and `licenses/`, plus
  `corresponding-source.json`. They must also offer the complete corresponding
  source for pimio, libumm, Exiv2, Expat, and zlib.
- Every other redistributed dependency must be GPL-3.0-compatible. The
  current bill of materials is: Qt LGPL-3.0, libheif/libde265 LGPL-3.0, the
  qt-heic plugin LGPL-2.0+, libavif/libaom/qt-avif BSD-2-Clause, LORE MIT.
  All of these are compatible. Each new dependency is checked against this
  rule in [dependency-bom.md](../dependency-bom.md).
- **Maintainer confirmation:** pimio's own source license. Any GPL-3.0-compatible
  license works; GPL-3.0-or-later is simplest. Add a top-level `LICENSE`
  before the first archive built with libumm is published.
- HEVC patent review remains a separate release gate.

### 7. ExifTool is located at runtime, not bundled (recommended)

libumm never bundles or redistributes ExifTool. It finds ExifTool at runtime:
first `BackendManager::configureExifTool`, then the `UMM_EXIFTOOL` environment
variable, then `PATH`. When ExifTool is missing, libumm reports
`backend_unavailable` / `unsupported_capability`. pimio follows the same model:

- Stop fetching and installing ExifTool and Strawberry Perl in release
  archives (`cmake/PimioExifTool.cmake`).
- `pimio-doctor` and an in-app status show whether ExifTool was found and which
  capabilities need it: video writes; HEIC, AVIF, CR3, JXL, and GIF writes;
  extended reads. They link to libumm's `tools/get-exiftool` helpers.
- A pimio setting may point to an ExifTool executable and is passed to
  `configureExifTool`.

**Maintainer confirmation:** the alternative is to keep bundling ExifTool
under its own Artistic/GPL terms and configure its path explicitly. Even then,
pimio must not depend on bundling for correctness: every ExifTool-only feature
must degrade the same way when ExifTool is missing.

### 8. Threading

libumm `Backend` instances are single-threaded and shared through the
`BackendManager` singleton. Until libumm documents a safe concurrent read
model, pimio sends all libumm calls through one metadata worker. This applies
to scan, watch, Save, and jobs. Concurrent reads are a tracked upstream
request.

## What this removes

The following go away once the migration is complete:

- the built-in parsers in `src/metadata/src/` (`image_reader`, `tiff_reader`,
  `iso_bmff_reader`, `xmp_reader`, `media_signature`, `field_set`);
- the `BuiltinMetadataReader`;
- `ExifToolMetadataWriter`;
- `cmake/PimioExifTool.cmake`;
- pimio's own sidecar precedence logic.

The fixed `core::MediaMetadata` fields (caption, rating, tags, camera, location,
…) are replaced by a canonical property model. libumm types stay inside
`src/metadata/`; core keeps a libumm-free model so that core, projection, and
UI tests stay independent of libumm.

## Rejected options

- **Keep the built-in reader plus the ExifTool writer.** This duplicates what
  libumm does, keeps two incompatible field models, and contradicts the
  no-reinvention rule.
- **libexiv2 directly.** It brings the same GPL-3.0 consequence without
  libumm's canonical model, casting, or ExifTool coverage.
- **Canonical-only display.** Hiding non-canonical metadata would make
  BMP/MKV/AVI/WAV/ASF and camera/technical fields invisible. The read-only
  view costs nothing extra because libumm provides it.

## What would reverse this

- libumm being discontinued, or its canonical model diverging from pimio's
  product needs in a way upstream declines to address.
- A product decision that pimio binaries must not be conveyed under GPL-3.0.
  libumm offers no Exiv2-free build, so this would require a different
  engine.
