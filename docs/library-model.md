# Library Identity and Authorization

A pimio Library is identified by the descriptor stored at the reserved
`records/.pimio-library.json` path in its LORE repository. The descriptor
contains a random library id, display name, format version, creation time, and
the stable id of the implicit v1 local user. Copying, moving, restoring, or
renaming the repository does not change either identity. Filesystem paths are
locators only.

Every pimio-created LORE checkpoint records its author id, pimio version, and
parent checkpoint id. Repositories created before these fields existed remain
readable; their history reports the author as `unknown` and leaves unavailable
version and parent data empty. Canonical history remains append-only.

## Authorization boundary

Library services evaluate four permissions per library:

- **Read** — inspect library records and derived views.
- **Write** — append canonical records and checkpoints.
- **Administer** — change library-level configuration and lifecycle state.
- **Share** — grant or revoke access when multi-user services exist.

The v1 policy grants all four permissions to the descriptor's implicit local
user and none to any other identity. There is no permission or user-management
UI. Later server and collaboration layers can replace the policy behind this
boundary without changing stored authors or service callers.

Projection databases, job queues, and thumbnail caches are derived data and use
the stable library id in their application-data paths. The Library Manager
stores known repository locators separately and revalidates identity from the
descriptor whenever it opens one. The repeated `--library` media-root form
remains a compatibility import entry point; it is never exposed as identity.
The in-process lifecycle boundary and backup format are documented in
[library-service-api.md](library-service-api.md).

## Original media storage

The v1 storage model is managed: importing media copies the original bytes into
the Library's LORE repository and commits them with the canonical record. Import
paths are provenance and discovery inputs, not durable Library dependencies.
The scanner copies new and changed originals through `DurableStore`; consumers
resolve the portable repository-relative location to the current checkout.
Deleting an import source after a successful commit does not remove the managed
item. Records written before managed ingest remain explicitly `referenced` and
readable; rescanning an available source migrates it, while a missing source
remains visibly incomplete rather than being relabeled or deleted.

Increment 7.8c repeated the large-binary gate on LORE 0.9.0 after removal of
pimio's whole-store rollback copy. Metadata commits no longer scale with the
corpus. Managed originals require a checkout copy plus an immutable store copy,
and a complete backup temporarily doubles that storage again; those costs are
accepted so one repository contains the complete Library. See
[decision 0005](decisions/0005-managed-versus-referenced-originals.md).
