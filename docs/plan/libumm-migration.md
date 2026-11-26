# libumm Metadata Migration Plan

This plan moves every metadata read and write in pimio onto
[libumm](https://github.com/dtmland/libumm). The decision record is
[0008](../decisions/0008-libumm-metadata-engine.md). Gaps found in libumm are
tracked in [libumm-upstream-requests.md](libumm-upstream-requests.md).

Each increment below (M0–M8) is sized for one implementation session. Each
one leaves `main` green and pushable. The increments are inserted before the
remaining v1 increments 9–12, which depend on them. See
[pimio-v1-implementation.md](pimio-v1-implementation.md) and
[progress.md](progress.md).

## Ground rules (apply to every increment)

1. **No reinvention.** If a step would need pimio to parse a container, map a
   tag, cast between photo and video, merge sidecars, detect conflicts, or
   choose a backend, stop. Record the need in the upstream-requests file and
   request it from libumm. Do not implement it in pimio.
2. **Canonical first, cross-media first.** New metadata features are written
   against libumm canonical ids and cross-media accessors. A feature defined
   only for photos or only for videos needs a stated reason, for example
   "libumm maps it only in one domain" with an upstream request linked.
3. **Edit only canonical properties; read everything.** pimio writes only
   canonical properties. It shows every non-canonical entry libumm reads, as
   read-only.
4. **Capabilities come from libumm.** pimio keeps no file-extension or format
   tables for metadata. It uses `umm::capabilities` /
   `capabilitiesForType` and reports libumm's reason when something is
   unavailable.
5. **libumm types stay inside `src/metadata/`.** `pimio::core`, projection,
   editing, app, and QML depend on pimio's own canonical model. Tests above the
   metadata module keep using fakes.
6. **One pin.** The libumm version, URL, and SHA-256 live only in
   `cmake/PimioUmm.cmake`. Every build context consumes that file. See the
   build rules in `.github/copilot-instructions.md`.
7. **Alpha data policy.** Projection schemas, LORE record JSON, and backups
   may change format. Note the break in the PR description; do not add
   migration code.

## Inventory: what is thrown away, retrofitted, or kept

| Area | Today | After migration |
| --- | --- | --- |
| `src/metadata/src/{image_reader,tiff_reader,iso_bmff_reader,xmp_reader,media_signature,field_set}` | Built-in parsers | **Deleted** |
| `BuiltinMetadataReader` | Implements `core::MetadataReader` | **Replaced** by `UmmMetadataReader` (libumm adapter) |
| `ExifToolMetadataWriter` | ExifTool JSON import | **Replaced** by `UmmMetadataWriter` (`umm::write`, `embedded_only`) |
| Sidecar precedence (`UserEdit > Sidecar > Embedded`) | In pimio | **Deleted**; libumm `merge_sidecar` and resolution |
| `core::MediaMetadata` fixed fields | caption/rating/tags/camera/… | **Retrofitted** into a canonical property model plus a read-only non-canonical entry list; libumm-free |
| `core::MetadataConflict` | pimio-computed | **Retrofitted** to carry libumm resolution/sources |
| `core::MetadataReader` / `MetadataWriter` boundaries | Present | **Kept**, with signatures updated to the canonical model and capabilities |
| `core::CaptureTime` | Wall clock + optional offset | **Kept**; filled from canonical `dateCreated` (libumm partial-precision `DateTime`); FS-time fallback stays in pimio |
| `editing::MetadataEditService` | Stages caption/rating/tags | **Retrofitted** to stage canonical property edits; fingerprint check, readback, and one-revision commit kept |
| `scan::Scanner` | Calls the reader if `supports()` | **Kept**; calls go through one metadata worker (0008 §8) |
| Projection columns + FTS | camera/rating/caption/lat-lon | **Retrofitted** to canonical-derived columns plus an FTS column for non-canonical text |
| `DetailView.qml` editor | Caption/rating/tags | **Retrofitted** to a canonical editor plus a read-only "All metadata" panel |
| `cmake/PimioExifTool.cmake`, ExifTool/Perl in archives | Bundled | **Removed** (if 0008 §7 is confirmed); runtime discovery |
| `tests/metadata/tst_metadata_golden.cpp` | Golden tests of parsers | **Rewritten** as adapter tests against libumm |
| `tests/metadata/tst_metadata_write.cpp` | ExifTool round trip | **Rewritten** for `umm::write` round trip |
| `tools/fixture_generator` EXIF builder | Writes fixtures | **Kept** (fixture independence is still useful) |
| `tests/support` `FakeMetadataReader` | Fake | **Kept**, updated to the new model |

## Increments

### M0 — Decisions, licensing, and spike (docs + throwaway branch)

- Get maintainer confirmation on the 0008 items:
  - the pimio source license (GPL-3.0-compatible) and adding `LICENSE`;
  - ExifTool runtime discovery versus bundling;
  - the sidecar read policy (`merge_sidecar`);
  - the RAW write policy.
- Spike on a throwaway branch: link libumm v0.1.2 with FetchContent in a
  scratch target. On all three CI platforms, confirm the following:
  1. `umm::read` on each fixture in `tests/fixtures` (canonical and
     non-canonical types, including BMP, MKV, AVI, and WAV if available);
  2. what `dumpUnmapped` returns for camera make/model/lens, orientation,
     dimensions, video duration, and audio, with and without ExifTool;
  3. build time, binary size, and MSVC/Clang/GCC warnings;
  4. behavior with concurrent `umm::read` calls;
  5. full-read cost per file on a large fixture set, to size U10.
- Record the findings in the upstream-requests file. File the requests using
  the ready-to-file text in
  [libumm-feature-requests.md](libumm-feature-requests.md), blocking ones
  (U1, U4) first.
- **Done when:** 0008's confirmation items are resolved, spike results are
  recorded, and blocking gaps are filed upstream.

### M1 — Build integration of the libumm pin in every context

- Add `cmake/PimioUmm.cmake` to hold the single libumm pin (version, URL,
  SHA-256) and `pimio_acquire_umm()`. It uses FetchContent with `URL_HASH`,
  `BUILD_SHARED_LIBS OFF`, and `UMM_BUILD_TESTS OFF`, and links `umm::umm`.
  Follow umm's `CMakeLists.txt` and its `tools/build/libumm.env` pattern,
  adapted to pimio's rule that pins live in `cmake/`. Use
  `UMM_REQUIRE_EXIV2=ON`; leave `UMM_REQUIRE_EXIFTOOL` OFF in shipped builds
  so that ExifTool stays optional at runtime.
- Optionally add `PIMIO_USE_SYSTEM_LIBUMM` (`find_package(umm CONFIG)`), the
  equivalent of umm's `UMM_CLI_USE_SYSTEM_LIBUMM`.
- Add libumm's build prerequisites to `tools/build/linux-packages.txt`.
  umm lists `perl`, `zlib1g-dev`, and `libexpat1-dev`. Check whether libumm's
  static Exiv2 build actually uses the system zlib/expat or fetches them.
  Confirm Windows needs (Perl is already provisioned by `pinned.ps1`) and
  macOS needs.
- Add a build contract in `tests/build/test_build_contracts.py`. It asserts
  that the pin exists once, that `CMakeLists.txt` includes it, and that no
  workflow or local harness repeats the literal.
- Build in CI, Release, Local Linux (container rebuilt), and Local Windows
  (Windows PowerShell 5.1 readers, plus a Sandbox run if available).
- Nothing calls libumm yet; the library is only linked into `pimio_metadata`.
- **Done when:** all contexts build with libumm, build contracts pass, and
  `docs/build-architecture.md` describes the pin.

### M2 — Canonical metadata model in core

- Replace the fixed fields of `core::MediaMetadata` with a libumm-free model:
  - canonical values keyed by libumm property id;
  - a domain value (photo/video/unknown);
  - per-property resolution and sources (embedded/sidecar, conflict);
  - a read-only list of non-canonical entries `{family, key, value}`.
- Provide typed cross-media getters for the properties pimio uses everywhere:
  `dateCreated`, `title`, `description`, `headline`, `keywords`, `rating`,
  `creator`, `locationCreated` (GPS), and `locationShown`. They are named after
  libumm's accessors, not pimio's old names (caption/tags).
- JSON round-trip for LORE records and backups. Alpha policy applies, so the
  old record format is not read.
- Update `FakeMetadataReader`, `core.serialization`, and `core.contracts`.
- **Done when:** core and its consumers compile against the new model using
  fakes, and the old field names are gone.

### M3 — libumm read adapter replaces the built-in reader

- Add `UmmMetadataReader : core::MetadataReader`. It calls `umm::read`,
  converts canonical values and the unmapped view into the M2 model, and maps
  `umm::ErrorCode` to `core::ErrorCode`. `supports()` comes from
  `umm::capabilities` and is true when libumm can read anything at all.
- Add a single metadata worker (0008 §8) that serializes libumm calls for
  scan, watch, and Save.
- Delete `BuiltinMetadataReader`, the parsers, and their private headers. Update
  `library_session.cpp`.
- Rewrite `metadata.golden` as adapter tests over the fixture corpus. They
  cover canonical values for photo and video, cross-media equivalence (the
  same canonical getters on JPEG and MP4), non-canonical entries present and
  read-only, non-canonical-only types (for example BMP), and libumm conflicts
  surfaced.
- ExifTool-dependent assertions are skipped with a visible reason when libumm
  reports `backend_unavailable`. CI installs ExifTool (see M7) so that they
  run there.
- **Done when:** scanning a library goes only through libumm, and the
  `metadata.*` tests pass on all platforms.

### M4 — Projection and search on canonical properties

- Projection columns are derived from canonical getters: capture time,
  rating, `locationCreated` GPS, and keywords. Camera filters use the
  non-canonical make/model entries (read-only data may still be indexed).
- FTS5 indexes canonical text (title, description, headline, keywords,
  creator) plus a column for non-canonical text values. This lets users find
  files by any metadata they can see.
- Filters behave the same for photos and videos.
- **Done when:** `projection.metadata` and the search tests pass using the new
  model, and a mixed photo/video query returns both.

### M5 — Write path via `umm::write`

- Add `UmmMetadataWriter : core::MetadataWriter`. It calls `umm::write` with
  `StoragePolicy::embedded_only`. It checks `WriteReport` and rereads with
  `UmmMetadataReader`. Capability checks come from
  `umm::capabilities(path)` and replace `supportsEmbeddedWrite`.
- Retrofit `MetadataEditService` to stage edits to canonical properties. The
  fingerprint check, the single LORE revision per Save, and reset on failure
  are kept.
- Delete `ExifToolMetadataWriter`.
- Rewrite `metadata.embedded_write` and `editing.metadata_save`:
  - round trips on JPEG, PNG, and TIFF (Exiv2 path);
  - MP4/MOV and HEIC (ExifTool path, skipped with a reason if ExifTool is
    absent);
  - checks that non-canonical entries are preserved byte-for-value after a
    canonical write;
  - checks that a type without canonical write is refused with libumm's
    reason.
- **Done when:** Save writes canonical properties for photos and videos
  through libumm only.

### M6 — UI: canonical editor, read-only "All metadata", capability gating

- Change the DetailView editor to edit canonical properties through
  cross-media labels (Title, Description, Keywords, Rating, Creator, Date
  created, Location). The same editor serves photos and videos.
- Add a collapsible **All metadata** inspector. It lists every non-canonical
  entry grouped by family, read-only, and can be copied. For types without a
  canonical mapping it is the only metadata panel.
- Add a conflict display with "choose value" that writes the chosen canonical
  value.
- When libumm reports a capability as unavailable, controls are disabled
  and show libumm's reason. "Install ExifTool to edit video metadata" is one
  such reason.
- Add manual-testing entries for these behaviors.

### M7 — Packaging, ExifTool discovery, notices, corresponding source

- Remove `cmake/PimioExifTool.cmake` and its `include`. Remove ExifTool and
  Perl staging from `release.yml` and the layout verification. Update the
  related build contracts. (Skip this if the maintainer chose bundling in M0;
  in that case configure the bundled path with `configureExifTool`.)
- Add a pimio setting for the ExifTool path, passed to `configureExifTool`.
  `pimio-doctor` reports ExifTool status and libumm capabilities and points
  to libumm's `tools/get-exiftool` helpers.
- CI installs ExifTool for tests, using the libumm/umm `get-exiftool` script
  with the pin from libumm's `backends.env`. That way ExifTool-dependent
  tests run without shipping ExifTool.
- Ship libumm's `share/doc/libumm/` (LICENSE, NOTICE.md,
  THIRD-PARTY-NOTICES.md, licenses/, corresponding-source.json) in every
  archive. Add pimio's `LICENSE` and a GPL-3.0 source offer, or a source
  archive with the release. Update `packaging/README-*.txt`.
- **Done when:** release archives pass layout verification with libumm
  notices, without ExifTool/Perl, and a packaged smoke test shows that
  ExifTool-free degradation behaves as designed.

### M8 — Cleanup and documentation pass

- Remove any remaining reference to the built-in reader or ExifTool writer
  from code, tests, and docs.
- Mark 0002 superseded in fact, and the migration increments complete in
  `progress.md`.
- Check the upstream-requests file: close resolved items and bump the libumm
  pin for any delivered ones.
- Run the repository metrics and refactor if needed.

## How the remaining v1 increments consume libumm

| v1 increment | Uses |
| --- | --- |
| 9 Timestamp repair | Read and write canonical `dateCreated` (photo and video via the same accessor). Keep libumm's partial precision and its no-invented-offset rule (H12). Batch shift writes through `umm::write`. If shift/offset helpers are needed, request them upstream. |
| 10 Video | MP4/MOV canonical read/write through libumm (needs ExifTool for writes). Duration, frame size, and audio come from canonical `iptc.video.*` technical properties where present, otherwise from the read-only view. Playback/trim stays with Qt Multimedia (media decoding, not metadata). |
| 11 Location | `locationCreated` (photo) / `locationShot` (video) GPS through the cross-media accessor; `locationShown` for depicted places. GPS-track import uses libumm's track import, not a pimio parser. |
| 12 Packaging/legal | GPL-3.0 conveyance, libumm notices, corresponding source, ExifTool runtime discovery (M7). |
