# 0007 — Prefer embedded metadata writes

Status: **accepted.**

Decision owner: repository maintainer.

## Decision

pimio should write standards-compatible metadata into managed originals rather
than create metadata sidecar files. A metadata adapter must preserve fields it
does not understand, replace the file atomically where the platform supports
it, leave the prior valid file intact on failure, and verify its output by
rereading it.

Sidecars are not a default fallback. Introducing or defaulting to sidecars
requires:

1. a documented format or workflow limitation that prevents a safe embedded
   update;
2. an interoperability and conflict-handling analysis; and
3. explicit approval from the repository maintainer.

The built-in reader selected by
[decision 0002](0002-metadata-adapter.md) remains the read adapter for now.
Increment 8 must evaluate a mature metadata library, including libexiv2, for
conflict-aware embedded writes. If that evaluation replaces the built-in reader
as well, decision 0002 must be superseded rather than rewritten. Licensing,
redistribution, and all supported build contexts remain release gates.

## Rejected sidecar-only experiment

An Increment 8 implementation attempted a custom XMP-sidecar-only write path. It
used same-directory atomic replacement, edit-session snapshots, a writer lock,
and preservation of XML outside pimio's own RDF description. The experiment
demonstrated that a sidecar can avoid modifying an original, but it was rejected
as the product direction because it:

- did not implement the planned embedded metadata path;
- split authoritative metadata between the managed original and an adjacent
  file;
- relied on a project-owned XML rewrite strategy instead of a mature metadata
  adapter; and
- did not satisfy the independent-tool and fault-injection evidence required
  for Increment 8.

The experiment is retained in repository history, not in production. Its image
recipe renderer, image export service, and explicit derivative provenance were
independent of sidecars and remain useful.

## Consequences

- Increment 8 remains incomplete until embedded writes and their acceptance
  evidence are implemented.
- Managed-original mutation requires stronger safeguards than a sidecar-only
  design: unrelated metadata must survive, failures must preserve the prior
  bytes, concurrent changes must be reported, and successful writes must be
  verified.
- libexiv2 is a serious candidate rather than an assumed choice. Its
  GPL-2.0-or-later licensing and linking strategy require explicit review before
  distribution, and any dependency must be provisioned consistently in CI,
  Release, Local Linux, and Local Windows.

## Increment 8 outcome

ExifTool 13.59 is the write adapter. It runs as a separate process from a
checksum-pinned upstream distribution and is redistributed under the Perl
Artistic License option. This avoids linking GPL-2.0-or-later libexiv2 into the
application while providing mature embedded EXIF/IPTC/XMP handling for JPEG,
PNG, and TIFF on every supported platform.

libexiv2 0.28.9 was evaluated and rejected for this path. Direct linking would
bring GPL-2.0-or-later obligations into the application, and its published
support matrix does not provide EXIF writes for PNG. ExifTool supports embedded
writes for all three containers and preserves metadata it does not edit, though
metadata block ordering and padding are not byte-stable.

pimio therefore writes only a private copy, rereads the result with the
production reader, and publishes it through the durable store. The store checks
the committed fingerprint before replacement, uses same-directory atomic
replacement, and restores the committed checkout after any failed commit.
Unsupported formats fail visibly; there is no automatic sidecar fallback.

The adapter updates the portable user fields delivered in this increment:
rating, caption, and tags. Timestamp repair and location editing remain assigned
to Increments 9 and 11.
