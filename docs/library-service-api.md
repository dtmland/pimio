# Library Service Boundary

The desktop UI consumes Library lifecycle operations through
`pimio::app::LibraryService` and the session facade, not through LORE,
filesystem, projection, or cache APIs. `LibraryManager` is the v1 local
implementation and also exposes the known-Library list as a Qt item model.

## Operations and identity

The service boundary provides create, discover/open, close, rename, move,
backup, and restore operations. Each request returns either a `LibraryInfo`
containing the stable id, display name, and current locator or a structured
`core::Error`. The id is the identity; the location is only the current
locator. Rename, move, backup, restore, and server promotion preserve the id.

`LibrarySession` owns the active browser/indexing composition. It closes that
composition before operations that require a quiescent repository and opens it
again afterward. QML receives the service model and session facade as context
properties, so it never opens a repository or derived database directly.

This request/response shape is the v2 seam: a client implementation can
serialize the same lifecycle requests and `LibraryInfo`/error responses to a
pimio Server. The UI-facing contract does not expose local implementation
types, but v1 remains entirely in-process. Networking, authentication, and user
management are intentionally absent.

## Persistence and derived state

The per-user registry stores known ids, names, and current locators. It is a
discovery aid, not the authority for identity: opening a Library reads and
validates `records/.pimio-library.json` from its LORE repository and refreshes
the registry from that descriptor. Missing registry locations remain visible
so the user can locate or restore them.

A `.pimio-backup` is a versioned, checksummed single-file archive of the
complete durable store. Backup holds the repository writer lock and omits only
that process-local lock file. Restore rejects unsafe paths and duplicate
entries, verifies every SHA-256 digest before publishing the destination,
asks LORE to reconstruct its checkout, and verifies the descriptor.

Projection databases, job queues, and thumbnail caches are excluded because
they are derived. Restore removes any derived state for the restored id,
rebuilds the projection from canonical records, and leaves the empty job queue
and thumbnail cache to be recreated normally. Managed current originals,
canonical records, organization, identity, and history come from the restored
repository. Promotion transfers the same current durable state; older
historical payloads remain subject to LORE's documented lazy hydration.
