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
the stable library id in their application-data paths. The current repeated
`--library` media-root arguments remain a repository locator until Increment
7.9 adds the Library Manager; they are never exposed as library identity.

## Original media storage

The current implementation references original media in configured roots;
originals are not copied into LORE. The repository is authoritative for library
identity, metadata, organization, edit recipes, and history, but it is not by
itself a backup of the media. A complete referenced-library backup must include
every referenced root and the information needed to reconnect it after restore.

Increment 7.8c repeated the large-binary gate on LORE 0.9.0 after removal of
pimio's whole-store rollback copy. Metadata commits no longer scale with the
corpus, but managed originals still require a checkout copy plus an immutable
store copy, and a complete backup temporarily doubles that storage again.
Version 1 therefore keeps referenced originals. See
[decision 0005](decisions/0005-managed-versus-referenced-originals.md).
