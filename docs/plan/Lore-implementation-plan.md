Pimio — Implementation Roadmap & Architectural Sequencing

1. Purpose

This document defines a proposed implementation sequence for Pimio.

The objective is to release a useful, polished product relatively early while ensuring that architectural decisions made during V1 do not prevent later expansion into:

* Multi-device operation
* Home-server hosting
* Mobile clients
* Multi-user libraries
* Studio collaboration
* Advanced permissions
* Review and approval workflows
* Remote and multi-site deployments

The guiding principle is:

Build the foundation for the long-term architecture early, but delay complex user-facing features until they are justified.

Pimio should therefore be architecturally extensible from V1 without being feature-complete from V1.

⸻

2. Version Strategy

A possible progression is:

Version	Primary Goal
V0.x	Technical foundation / prototype
V1.0	Excellent single-user desktop application
V1.x	Refinement, reliability, migration, backup
V2.0	Home server and multi-device operation
V2.x	Mobile clients
V3.0	Multi-user / Studio capabilities
V3.x	Advanced collaboration
V4+	Advanced distributed/cloud capabilities

The exact version numbers should remain flexible.

The important distinction is between architectural milestones and marketing releases.

⸻

3. V0.x — Technical Foundation

V0.x should establish the core architecture before substantial UI development.

Required Components

Lore Integration

Establish a stable abstraction layer between Pimio and Lore.

Pimio should not scatter direct Lore API calls throughout the application.

Instead:

Pimio Application
       │
       ▼
Pimio Storage Abstraction
       │
       ▼
Lore

This makes Lore replaceable if necessary and prevents Lore-specific concepts from contaminating the application model.

Library Identity

Every library receives a stable unique identifier.

The identifier must survive:

* Copying
* Backup
* Restoration
* Migration
* Server relocation

The library’s current network location should not be its identity.

Asset Identity

Every media asset receives a stable identity independent of its current filename or filesystem location.

For example:

Asset ID
   │
   ├── Original
   ├── Derivatives
   ├── Metadata
   └── Revisions

This becomes critical later when files move, are renamed, or are edited collaboratively.

Revision Identity

Every meaningful modification should produce an identifiable revision.

A revision should have enough information to establish:

* Parent revision
* Author
* Timestamp
* Application version
* Change information

V1 may expose only a simple history interface, but the underlying model should support multiple authors.

Application Metadata Model

Define which metadata is:

Canonical

and which is:

Derived/rebuildable.

This distinction should be established before V1.

⸻

4. V1.0 — Single-User Desktop

V1 should focus aggressively on making Pimio an excellent standalone application.

Primary Features

Library Management

* Create library
* Open library
* Close library
* Switch libraries
* Rename library
* Move library
* Backup library
* Restore library

Media Management

* Import photographs
* Import videos
* Browse media
* Search
* Albums
* Tags
* Ratings
* Metadata editing

Versioning

* Preserve originals
* Record meaningful edits
* Browse history
* Restore previous versions
* Compare revisions where practical

Derivatives

Generate:

* Thumbnails
* Previews
* Display versions
* Video proxies
* Other application-specific derivatives

Editing

Provide initial non-destructive editing capabilities for photographs and basic video operations.

Avoid attempting to build a professional-grade video editor in V1.

⸻

5. V1 Architectural Requirements for Future Versions

Even though V1 is single-user, several concepts should be implemented correctly from the beginning.

5.1 Users

V1 does not need a user-management UI.

However, the underlying revision model should allow:

Author = User ID

rather than assuming:

Author = "the current user"

V1 can simply have one implicit user.

Later:

Alice
Bob
Carol

can be introduced without rewriting revision history.

⸻

6. 5.2 Permissions

V1 does not need configurable permissions.

Nevertheless, the server/application API should have a conceptual authorization boundary.

For example:

Library
 ├── read
 ├── write
 ├── administer
 └── share

V1 can simply grant all permissions to the local user.

Later, those permissions can become configurable.

⸻

7. 5.3 Immutable History

Avoid destructive modification of canonical revisions.

Prefer:

Revision 1
    ↓
Revision 2
    ↓
Revision 3

rather than modifying Revision 2 in place.

This makes future:

* collaboration
* auditing
* review
* rollback
* branching
* conflict handling

much easier.

⸻

8. 5.4 Asset Relationships

V1 should explicitly model relationships between assets and derivatives.

For example:

Asset A
 ├── Original
 ├── Thumbnail
 ├── Preview
 └── Export

For video:

Asset B
 ├── Original
 ├── Proxy
 ├── Trimmed Version
 └── Thumbnail

This should not be inferred solely from filenames.

⸻

9. 5.5 Rebuildable Indexes

Pimio should assume that search/index databases can be destroyed and reconstructed.

For example:

Lore Repository
       │
       ├── Canonical media
       ├── Metadata
       ├── Application state
       └── History
              │
              ▼
        Rebuild indexes
              │
              ├── Search
              ├── Faces
              ├── AI
              └── Thumbnails

This should be a fundamental V1 property.

⸻

10. V1.1 — Reliability and Portability

After the initial application is usable, focus on making the library extremely dependable.

Priorities:

* Robust backup
* Restore verification
* Library integrity checking
* Migration between computers
* Export/import testing
* Recovery from interrupted operations
* Background indexing
* Storage monitoring
* Cache management
* Improved version history

A user should be able to trust a Pimio Library before Pimio begins adding complicated collaboration features.

⸻

11. V2.0 — Home Server

V2 introduces the headless Pimio Server.

Architecture:

Desktop ──┐
Laptop ───┼──> Pimio Server ──> Lore
Mobile ───┘

New Features

Server

* Headless operation
* Multiple libraries
* Remote authentication
* Library discovery
* Remote browsing
* Remote media access
* Background processing

Desktop

* Add remote server
* Browse remote libraries
* Open remote library
* Copy library locally
* Synchronize where supported
* Move library between local and remote storage

Server Management

* Storage monitoring
* Library management
* Backup configuration
* Server health
* Connection management

⸻

12. V2 Architectural Preparation for Studio

Even if V2 remains primarily personal/family oriented, this is where the multi-user architecture should become real.

Introduce:

User Identity

Server
 ├── Alice
 ├── Bob
 └── Carol

Library Membership

Library A
 ├── Alice: owner
 ├── Bob: editor
 └── Carol: viewer

Authentication

Implement a real authentication mechanism rather than relying on trusted local-network access.

Authorization

Implement the basic permission model.

The initial UI can remain simple.

⸻

13. V2.x — Mobile

Once the server/API architecture is stable, mobile clients can be introduced.

The mobile application should preferably use the same Pimio service API rather than directly manipulating Lore repositories in ordinary operation.

Potential capabilities:

* Camera import
* Library browsing
* Favorites
* Albums
* Metadata
* Basic edits
* Upload
* Offline access
* Background synchronization

The mobile application should not need to understand the internal storage architecture.

⸻

14. V3.0 — Studio

V3 introduces true multi-user collaboration.

Users and Roles

Potential roles:

* Owner
* Administrator
* Editor
* Contributor
* Reviewer
* Viewer

The exact role model should remain flexible.

Asset Permissions

Potentially allow permissions at:

* Server level
* Library level
* Album level
* Asset level

Fine-grained permissions should be introduced only if real workflows justify them.

⸻

15. V3 — Collaboration

Introduce explicit collaboration concepts.

Potential features:

Ownership

Who owns a library or asset?

Attribution

Who made a particular change?

Review

Draft
  ↓
Submitted
  ↓
Under Review
  ↓
Approved

Assignments

Asset
  ↓
Assigned to Alice
  ↓
Edited
  ↓
Submitted to Bob

Audit

Users can see:

* Who changed an asset
* What changed
* When it changed
* Which revision resulted

⸻

16. Conflict Handling

Conflict handling should be introduced only after the basic versioning model is proven.

The underlying architecture should already allow:

Original
   ├── Alice's Revision
   └── Bob's Revision

The application can then decide how to present the conflict.

Possible strategies:

* Keep both revisions
* Select one as canonical
* Compare revisions
* Create a new merged revision
* Require human review

Pimio should avoid attempting automatic media merging unless the media type makes it safe.

⸻

17. V3.x — Studio Workflow

Once the underlying collaboration system is stable, add workflow functionality such as:

* Review queues
* Approval
* Rejection
* Comments
* Assignments
* Shared collections
* Client review portals
* Publishing workflows
* Revision comparison
* Change notifications

These should be application-layer features rather than responsibilities of Lore.

⸻

18. V4+ — Advanced Deployment

Later versions could explore:

Multiple Pimio Servers

Studio A
     │
     ├── Library A
     └── Library B
Studio B
     │
     ├── Library C
     └── Library D

Potential capabilities:

* Server-to-server replication
* Disaster-recovery replicas
* Geographic redundancy
* Cloud hosting
* Selective synchronization
* Off-site backup
* Multi-location studios

These features should only be attempted once the simpler client/server model is mature.

⸻

19. Features That Should NOT Be Prematurely Implemented

To keep V1 achievable, deliberately postpone:

* Complex permissions
* Team administration
* Approval workflows
* Asset locking
* Client portals
* Multi-site replication
* Sophisticated conflict resolution
* Enterprise administration
* Distributed deployment
* Advanced cloud infrastructure

However, the data model should not make these impossible.

This is the central distinction between:

“Not implemented yet”

and

“Architecturally impossible.”

Pimio should always aim for the former.

⸻

20. Critical Early Decisions

The following decisions have disproportionate long-term importance and should be resolved before V1 architecture is considered complete.

1. Library identity

A library must have a persistent identity independent of location.

2. Asset identity

Assets must have persistent identities independent of filenames.

3. Revision identity

Revisions must have stable identities and parent relationships.

4. Author identity

Every revision should have an author field, even if V1 has only one implicit user.

5. Canonical versus derived data

The system must clearly identify what can be rebuilt.

6. Storage abstraction

Lore should be accessed through a Pimio storage abstraction rather than being embedded throughout the application.

7. Service API

The Pimio client/server API should be designed early enough that mobile and remote clients do not require a later architectural rewrite.

8. Authentication boundary

Even if V1 is single-user, the server architecture should have a place for authentication.

9. Authorization boundary

Even if V1 grants unrestricted access, the architecture should have a place for authorization.

10. Event/history model

Application events and revisions should preserve enough information to support future audit and collaboration features.

⸻

21. The Most Important V1 Principle

V1 should not attempt to build Studio.

It should build the foundations that Studio will eventually require.

The desired progression is:

V1
Single user
    │
    ▼
Excellent local library

followed by:

V2
Same library
    │
    ▼
Remote server
    │
    ▼
Multiple devices

followed by:

V3
Same library
    │
    ▼
Multiple users
    │
    ▼
Collaboration

The underlying Library and Lore repository remain the same conceptual object throughout.

⸻

22. Recommended Development Philosophy

Pimio should follow three rules.

Rule 1 — Build the durable foundation early

Identity, history, storage abstraction, metadata, and API boundaries are difficult to retrofit.

Build these correctly from the beginning.

Rule 2 — Delay workflow complexity

Permissions, approvals, assignments, collaboration, and enterprise administration can wait.

They should be layered onto the foundation once the core product is proven.

Rule 3 — Never make users understand the infrastructure

The architecture may become sophisticated.

The user experience should remain simple.

A user should be able to think:

“This is my Family Library.”

without needing to know whether it currently resides:

* on their laptop,
* on their home server,
* on a studio server,
* or somewhere else entirely.

⸻

23. Target Architectural Evolution

The ultimate architecture should be capable of evolving from:

Pimio
└── Library
    └── Embedded Lore

to:

Pimio
│
├── Local Libraries
│
├── Home Server
│   ├── Library A
│   └── Library B
│
└── Studio Server
    ├── Library C
    └── Library D

and eventually:

                    Pimio Clients
                 /       |       \
                /        |        \
          Server A   Server B   Server C
             │          │          │
          Libraries   Libraries   Libraries
             │          │          │
            Lore       Lore       Lore

without requiring the core Library abstraction to change.

⸻

24. Final Recommendation

The first production release should be intentionally modest in visible functionality but ambitious in architecture.

V1 should deliver:

* Excellent desktop application
* Local embedded Lore
* Multiple libraries
* Durable versioned media
* Non-destructive editing
* Portable libraries
* Reliable backup and restoration
* Rebuildable indexes
* Clean storage abstraction
* Stable asset/library/revision identities
* A future-proof application service boundary

V2 should add:

* Headless Pimio Server
* Remote libraries
* Multiple devices
* Authentication
* Basic permissions
* Mobile clients

V3 should add:

* Multiple users
* Collaboration
* Roles
* Review
* Approval
* Assignments
* Audit capabilities

Later versions should explore:

* Multi-server synchronization
* Cloud hosting
* Geographic redundancy
* Advanced collaborative workflows
* Enterprise-scale deployments

The fundamental architectural goal remains unchanged throughout:

A Pimio Library is a durable, portable, versioned digital-media collection backed by a Lore repository.

Everything else—desktop versus server, local versus remote, personal versus studio, one user versus many users—is an increasingly sophisticated way of interacting with that same fundamental object.