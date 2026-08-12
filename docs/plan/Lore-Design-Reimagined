Pimio — Versioned Digital Asset Management Platform Proposal

1. Vision

Pimio is a cross-platform digital asset management application for photos, videos, and potentially other large digital assets.

Pimio combines a simple, user-friendly media-library experience with a durable, version-controlled storage architecture based on Lore.

The central architectural principle is:

One Pimio Library corresponds to exactly one Lore repository.

Users interact with Libraries, not repositories, commits, branches, or storage engines. Lore provides the underlying durable storage, version history, content deduplication, and synchronization capabilities while Pimio provides the media-specific semantics and user experience.

The result is intended to provide the simplicity of a traditional photo-management application while offering substantially stronger preservation, history, portability, backup, and collaboration capabilities.

⸻

2. Core Concepts

2.1 Library

A Library is Pimio’s fundamental unit of organization.

A library contains:

* Original photographs
* Original videos
* Modified versions
* Video trims and derived versions
* Metadata
* Albums
* Ratings
* Tags
* Organizational information
* Thumbnails and previews
* Other application-generated derivatives
* Potentially AI-generated metadata and analysis

Internally, every library corresponds to exactly one Lore repository.

The user should never need to understand this relationship.

2.2 Lore Repository

Lore is Pimio’s durable storage and version-control layer.

Lore is responsible for storing and versioning the underlying content. Pimio is responsible for understanding what that content means.

For example, Lore knows that a particular file changed between revisions.

Pimio knows that:

IMG_4821 is an original photograph, and revision 17 is the edited version with a crop, exposure adjustment, and rotation.

This separation keeps media-specific application semantics out of the storage engine.

⸻

3. Application Architecture

Pimio should support three progressively more capable deployment configurations.

3.1 Pimio Desktop — Standalone

The Pimio desktop application contains:

* Pimio client
* Lore client
* Embedded Lore server
* Pimio application services required for local operation

Everything runs on the user’s computer.

The user sees only Pimio.

Pimio Desktop
├── Pimio Application
├── Lore Client
└── Embedded Lore Server
       └── Library / Lore Repository

This is the default “lone wolf” configuration.

It requires no external server and allows the user to create, browse, edit, and manage libraries entirely locally.

⸻

4. Remote Pimio Servers

Pimio Desktop can connect to a remote Pimio Server.

A Pimio Server is a headless deployment of the Pimio application that contains an embedded Lore server.

Pimio Desktop
       │
       ▼
Pimio Server
       │
       ▼
Embedded Lore Server
       │
       ├── Library A
       ├── Library B
       └── Library C

A remote Pimio Server can therefore host multiple independent Pimio Libraries.

This configuration is appropriate for:

* Home servers
* NAS systems
* Always-on personal computers
* Small offices
* Photography studios
* Shared family libraries
* Larger collaborative environments

⸻

5. Direct Lore Connectivity

Pimio should also optionally support connecting directly to a Lore repository without requiring a Pimio Server.

Pimio Desktop
       │
       ▼
Lore Server
       │
       └── Lore Repository

This provides an advanced or lower-level integration path.

The experience may necessarily be more limited because Pimio-specific server functionality is unavailable.

Nevertheless, this mode could be valuable for:

* Advanced users
* Existing Lore deployments
* Simple repository access
* Recovery and migration
* Tools that need to inspect or manipulate a Pimio-compatible repository without running the complete Pimio server stack

The direct-Lore mode should not be required for ordinary users.

⸻

6. Library Connections

Pimio should distinguish three different concepts when connecting to remote libraries.

6.1 Open Remotely

The user’s Pimio application works against a remote library while the remote server remains authoritative.

Pimio Desktop
      │
      ▼
Remote Pimio Server
      │
      ▼
Remote Library

The user does not necessarily maintain an independent local copy.

6.2 Make a Local Copy

The user obtains a local copy of the remote library.

This is useful for:

* Taking a laptop offline
* Traveling
* Creating a local working environment
* Migration
* Disaster recovery

The local copy should retain its identity as the same library where the underlying Lore capabilities permit this.

6.3 Mirror / Synchronize

The user maintains a local copy while periodically synchronizing with the remote library.

This mode should be exposed only to the extent that Lore’s actual synchronization semantics support it. Pimio should not imply Git-style peer-to-peer synchronization if Lore does not provide it.

The distinction between these modes should be explicit in the user interface.

⸻

7. Library Manager

Pimio should provide a Library Manager that treats libraries as portable first-class objects.

A user might see:

Libraries
  Family
  Photography
  Video Archive
  Work
  Archive 2020–2025

Libraries may be located on:

* The current computer
* A home server
* A studio server
* Another remote Pimio server
* Potentially a direct Lore server

The location should be an implementation detail rather than part of the user’s conceptual model.

A library should have a stable unique identity independent of its current physical location.

This allows Pimio to recognize a library after it has been:

* Moved
* Restored
* Copied
* Migrated
* Re-hosted on another server

⸻

8. Multi-Server Support

A Pimio application should be able to maintain connections to multiple Pimio servers.

For example:

Pimio Desktop
│
├── Local
│   ├── Family
│   └── Personal
│
├── Home Server
│   ├── Family
│   └── Video Archive
│
└── Studio Server
    ├── Client Work
    ├── Stock
    └── Archive

The application should present these as libraries rather than forcing the user to think about server topology.

This allows one Pimio installation to operate across an entire collection of personal and organizational libraries.

⸻

9. Headless Pimio Server

The Pimio Server is a headless deployment intended for machines that primarily provide storage and services.

It contains:

* Pimio application services
* Lore client functionality
* Embedded Lore server
* Library management
* Indexing services
* Background processing
* Media derivative generation
* Search services
* Authentication and permissions where appropriate

A single Pimio Server may host multiple independent libraries.

Each library remains an independent Lore repository.

⸻

10. Durable Data Versus Rebuildable Data

A critical architectural principle is to distinguish canonical data from derived data.

The Lore repository should contain the information required to reconstruct the library.

Pimio may additionally maintain disposable databases and caches for performance.

Examples include:

* Search indexes
* Thumbnail caches
* Face-recognition indexes
* AI indexes
* Full-text indexes
* Waveform caches
* Transcoding caches

These should be considered rebuildable.

If a Pimio database becomes corrupted, Pimio should ideally be able to reconstruct it from the canonical repository.

This makes the Lore repository the ultimate source of truth.

⸻

11. Portable Library and Disaster Recovery

A major goal is for a Pimio Library to be a self-contained, portable unit.

A user should eventually be able to:

1. Back up a library.
2. Install Pimio on another computer.
3. Restore or import the library.
4. Have Pimio reconstruct the library.
5. Recover the media, metadata, organizational structure, and history.

The user should not have to separately understand:

* Media directories
* Database backups
* Thumbnail directories
* Search indexes
* Application caches
* Version-control databases

The library itself is the durable unit.

⸻

12. Versioned Media Model

Pimio should preserve originals rather than destructively modifying them.

For photographs, a library might contain:

* Original RAW
* Original JPEG
* Edited representation
* Exported representation
* Thumbnail
* Preview
* Metadata
* Editing information

For video:

* Original video
* Trimmed version
* Edited version
* Proxy
* Thumbnail
* Waveform
* Subtitles
* Chapter information

Lore does not need to understand the semantic relationship between these objects.

Pimio maintains those relationships.

⸻

13. Non-Destructive Editing

Where practical, Pimio should represent edits as operations or recipes rather than immediately generating entirely new copies of large media files.

For example:

Original
   │
   └── Edit Recipe
        ├── Crop
        ├── Rotate
        ├── Exposure
        └── Color adjustment

Pimio can generate rendered derivatives when necessary.

This reduces unnecessary storage consumption while preserving the complete editing history.

⸻

14. Derivative Assets

Pimio should explicitly model relationships between original assets and their derivatives.

Examples include:

Photograph

Original RAW
   ├── Full-resolution preview
   ├── Display JPEG
   ├── Thumbnail
   └── Web export

Video

Original Video
   ├── Full-resolution render
   ├── Proxy
   ├── Trimmed version
   ├── Thumbnail
   └── Web version

Lore provides storage and versioning for these files.

Pimio provides the semantic relationship between them.

This allows the application to select the appropriate representation depending on context.

⸻

15. Home Server and Studio Server

The distinction between Home Server and Studio Server should primarily be a matter of scale and collaboration requirements rather than fundamentally different architectures.

Home Server

Typical characteristics:

* One person or family
* Trusted users
* Multiple devices
* Shared libraries
* Relatively simple permissions
* Desktop, laptop, phone, and tablet access

Users may be allowed to make modifications.

Studio Server

The same basic architecture can support:

* Multiple independent users
* User accounts
* Fine-grained permissions
* Audit history
* Review and approval workflows
* Asset assignments
* Collaboration controls
* Potential locking or conflict-management features

The underlying storage model remains the same.

The application simply enables additional collaboration functionality.

⸻

16. Deployment Progression

Pimio should allow a user to grow into more sophisticated configurations without changing the fundamental library model.

Stage 1 — Personal

Pimio Desktop
└── Embedded Lore

Stage 2 — Home Server

Pimio Desktop ──┐
Pimio Laptop ───┼──> Pimio Server
Pimio Mobile ───┘        │
                         └── Lore

Stage 3 — Multi-user / Studio

User A ──┐
User B ──┼──> Pimio Server ──> Lore
User C ──┤
User D ──┘

Stage 4 — Multiple Servers

Pimio Desktop
│
├── Home Pimio Server
│     ├── Family
│     └── Personal
│
├── Studio Pimio Server
│     ├── Client A
│     └── Client B
│
└── Direct Lore Server
      └── Archive

The application remains fundamentally the same throughout.

⸻

17. Architectural Separation

Pimio should maintain a strong boundary between three layers.

Pimio UI and Application Layer

Responsible for:

* Photos
* Videos
* Albums
* Metadata
* Editing
* Search
* Organization
* User experience

Pimio Service Layer

Responsible for:

* Media processing
* Indexing
* Derivative generation
* Authentication
* Permissions
* Collaboration
* Application-specific APIs

Lore Storage Layer

Responsible for:

* Durable content
* Version history
* Repository management
* Deduplication
* Storage
* Synchronization
* Repository integrity

This separation allows Pimio to exploit Lore without becoming tightly coupled to Lore-specific concepts throughout the application.

⸻

18. Design Principle: Lore Should Be Invisible

The typical user should never need to know that Pimio uses Lore.

They should think in terms of:

Libraries, photos, videos, edits, albums, and history.

Not:

repositories, commits, branches, remotes, objects, or caches.

Lore should be exposed only where its capabilities provide value to advanced users.

⸻

19. Long-Term Possibilities

The architecture leaves room for future capabilities including:

* Mobile clients
* Cloud-hosted Pimio servers
* Multiple-server replication
* Collaborative editing
* AI-assisted organization
* Automatic photo and video analysis
* Intelligent duplicate detection
* Remote backup
* Selective local caching
* Offline operation
* Cross-device synchronization
* Multi-site studio deployments
* Library migration between servers

The important property is that these capabilities do not require changing the fundamental relationship:

Pimio Library = Lore Repository

⸻

20. Summary

Pimio should be designed as a media-management platform built on top of Lore rather than as a thin graphical interface around Lore.

The user-facing abstraction is the Library.

The durable storage abstraction is the Lore repository.

The application can operate entirely locally, connect to a Pimio server, or—where appropriate—connect directly to a Lore server.

A single server can host multiple independent libraries, while a single Pimio installation can connect to multiple servers.

This produces a scalable progression:

One user → one computer → one library

One user → multiple devices → home server → multiple libraries

Multiple users → studio server → collaborative libraries

without requiring fundamentally different storage architectures.

The ultimate goal is for a Pimio Library to be a durable, portable, versioned representation of a user’s digital-media collection—one that can be moved, backed up, restored, hosted remotely, and eventually shared collaboratively without requiring the user to understand the infrastructure underneath it.