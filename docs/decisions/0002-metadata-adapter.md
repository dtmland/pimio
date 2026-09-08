# 0002 — A built-in metadata reader for v1's read path

Status: **accepted.** Recorded during Increment 5, which delivers metadata
read, query, and search.

Decision owner: repository maintainer.

## Decision

Read metadata with a small built-in parser, `pimio::metadata`, instead of
integrating libexiv2 (or any other metadata library) for v1's read path. The
`core::MetadataReader` boundary is unchanged, so the decision is reversible by
supplying a different implementation of it.

This decision covers reading only. The write path (Increment 8) and anything
requiring a decoder — thumbnails, RAW previews, video frames — are explicitly
out of scope here and are free to reach a different conclusion.

## Context

[pimio-v1-tools-environment.md](../plan/pimio-v1-tools-environment.md) lists
libexiv2 as the obvious candidate and records the concern that matters:
libexiv2 is GPL-2.0-or-later. pimio ships on Linux, Windows, and macOS, so any
metadata dependency must be built, redistributed, and license-reviewed on all
three, and `docs/dependency-bom.md` already records legal review as a release
gate that CI cannot certify.

What Increment 5 actually needs is narrow:

- capture time and its UTC offset, or the honest absence of one;
- camera make, model, and lens;
- orientation, pixel dimensions;
- GPS coordinates;
- video duration and audio-track presence;
- XMP sidecar values, with explicit precedence and recorded conflicts.

All of that lives in container headers — JPEG APP1/EXIF, TIFF IFDs, XMP's XML,
and ISO base media boxes. None of it requires decoding a single pixel or audio
sample.

## Options considered

| Option | Cost | Consequence |
| --- | --- | --- |
| libexiv2 | A GPL dependency built and shipped on three platforms, plus a license review before distribution | Broadest format coverage, including formats v1 does not index |
| Qt alone | None | Insufficient: Qt exposes no EXIF, XMP, or container metadata API |
| Built-in header parsers | Roughly a thousand lines of bounds-checked parsing, maintained by this project | No new dependency, no new license obligation, identical behaviour on all three platforms |

## Consequences

Accepted:

- Format coverage is limited to what pimio indexes today: JPEG, PNG, TIFF and
  TIFF-based RAW containers, ISO base media video, and XMP sidecars. A file the
  parser does not recognize becomes a visible `UnsupportedMedia` record rather
  than a silent omission, so the limit is observable rather than hidden.
- pimio owns the parsing bugs. This is mitigated by the committed fixture
  corpus, by golden tests (`metadata.golden`) that pin what is read out of each
  fixture, and by parsers that validate every length and offset against the
  buffer before using it. Metadata comes from files pimio did not create, so
  hostile input is the design assumption, not an edge case.
- Vendor-specific maker notes are not read. Nothing in v1 depends on them.

Gained:

- No new runtime dependency, so `docs/dependency-bom.md` gains no row and
  packaging gains no library, notice, or license obligation.
- The same code path runs on all three platforms, so a metadata bug cannot be
  platform-specific.
- No third-party build to reproduce in CI, which keeps the Windows and macOS
  jobs as simple as the Linux one.

## What would reverse this

Any of the following makes a real metadata library the better trade, and this
record should then be superseded rather than edited:

- A requirement to read formats whose metadata is not in a plain container
  header, such as HEIF/AVIF item properties or vendor maker notes.
- Increment 8 concluding that conflict-aware embedded writing — preserving
  every tag pimio does not understand while rewriting the ones it does — is not
  safely achievable with parsers of this size.
- Evidence from a real library that the built-in reader misreads files people
  actually own.

The reversal cost is bounded: `core::MetadataReader` is the only surface the
rest of pimio sees, and `pimio::metadata` is the only implementation of it.
