# Universal Media Metadata Abstraction Layer
## Concept Plan

### 1. Project Purpose

Create an open-source library that provides applications with a **single, standards-based API for reading, writing, synchronizing, and exchanging metadata across photos, videos, audio, and related media formats**.

The project does **not** attempt to create another metadata standard.

Instead, it adopts existing authoritative definitions wherever they exist and provides the software layer necessary to use those definitions consistently across different file formats and metadata technologies.

The central idea is:

                    Application
                         │
                         ▼
              Universal Metadata API
                         │
                  Semantic model
                         │
          ┌──────────────┼──────────────┐
          │              │              │
        IPTC            EXIF            XMP
          │              │              │
          └──────────────┼──────────────┘
                         │
                Format / container
                    abstraction
                         │
             ┌───────────┴───────────┐
             │                       │
          Exiv2                  ExifTool
             │                       │
             └───────────┬───────────┘
                         │
              Embedded / sidecar
                         │
                  Media file

The library becomes the **application-facing abstraction layer**, while IPTC, XMP, EXIF, QuickTime/MP4, etc. remain the authorities for the underlying metadata standards.

---

# 2. The Most Important Design Principle

## Do not invent metadata definitions unnecessarily.

Whenever an established standard already defines a property, the library should adopt that definition rather than creating a competing one.

For example:

- Title
- Description
- Creator
- Copyright Notice
- Credit Line
- Date Created
- Location
- Person Shown
- Digital Source Type
- Licensor
- Copyright Owner
- Keywords
- Rating

should preferentially derive their semantics from the appropriate IPTC specifications.

The IPTC Photo Metadata Standard explicitly defines each property with its **name, semantics, requirements, UI/help text, and technical representation**.

Primary standard:

[IPTC Photo Metadata Standard 2025.1](https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html)

The current Photo Metadata Standard includes:

- IPTC Core 1.5
- IPTC Extension 1.9

---

# 3. IPTC Becomes the Semantic Foundation

The project should explicitly document:

> "Where this library exposes an IPTC-defined property, the property's meaning and semantics are inherited from the applicable IPTC specification."

That means the library does not redefine:

    creator
    description
    headline
    creditLine
    copyrightNotice
    copyrightOwner
    dateCreated
    city
    country
    countryCode
    location
    personShown
    keywords
    rating
    etc.

Instead it maps those concepts into a programming API.

For example:

    metadata.description

would mean the same thing as the IPTC-defined Description/Caption property.

Likewise:

    metadata.creator

would use the IPTC definition of Creator/Image Creator rather than a new project-defined definition.

This is one of the project's strongest adoption strategies.

---

# 4. Use IPTC's Mapping Work Instead of Recreating It

This is arguably the most valuable existing work for the project.

IPTC publishes **Photo Metadata Mapping Guidelines** that explicitly map IPTC properties to other metadata systems, including EXIF and Schema.org.

[IPTC Photo Metadata Mapping Guidelines](https://www.iptc.org/std/photometadata/documentation/mappingguidelines/)

Therefore the project should NOT initially maintain its own hand-created table such as:

    IPTC Creator → EXIF Artist → XMP dc:creator

Instead, the project should establish:

    IPTC property
          │
          ├── IPTC-defined semantics
          │
          ├── IPTC-defined XMP representation
          │
          └── IPTC mapping guidance
                  │
                  ├── EXIF
                  └── other representations

The project's mapping implementation can then be generated, validated, or at minimum cross-checked against the IPTC mappings.

This dramatically reduces the amount of metadata knowledge the project has to invent and maintain.

---

# 5. Use IPTC's Machine-Readable Reference

This is particularly important for implementation.

IPTC provides a **Technical Reference** containing machine-readable representations of the Photo Metadata Standard in **JSON and YAML**.

[IPTC Photo Metadata Technical Reference](https://iptc.org/std/photometadata/documentation/techreference/)

That creates the possibility of an automated process such as:

    IPTC Technical Reference
              │
              ▼
         Schema importer
              │
              ▼
       Project metadata registry
              │
         ┌────┴────┐
         ▼         ▼
      API docs   Runtime mappings

Instead of manually typing hundreds of metadata definitions into the project.

The project could record, for each adopted property:

    standard: IPTC
    standard_version: 2025.1
    schema: Core
    property: Creator
    definition: ...
    xmp_namespace: ...
    xmp_property: ...

The important point is that **the source of truth remains IPTC**.

---

# 6. XMP Is the Primary Interchange Representation

XMP should be treated differently from IPTC.

**IPTC defines metadata properties.**

**XMP provides a metadata framework in which those properties can be represented.**

IPTC itself explains that XMP does not define the metadata properties; schemas such as IPTC's use XMP to express them. XMP can be embedded in media or stored in sidecar files.

Therefore:

    IPTC
      = meaning

    XMP
      = representation mechanism

The project should therefore support:

    IPTC property
           ↓
    XMP representation
           ↓
    embedded XMP
           or
    .xmp sidecar

rather than treating "XMP" itself as the semantic authority.

---

# 7. Recognize the IPTC + Adobe History

There is also an excellent historical precedent for this architecture.

IPTC and Adobe **jointly developed the IPTC Core Schema for XMP in 2004**, translating the existing IPTC concepts into the XMP environment.

So the project can legitimately build upon an ecosystem that was deliberately designed to bridge:

    IPTC semantics
           ↓
    XMP representation
           ↓
    image software

The proposed library essentially extends that idea upward:

    IPTC semantics
           ↓
    Universal Media Metadata API
           ↓
    XMP / EXIF / QuickTime / etc.
           ↓
    actual files

---

# 8. Video Should Adopt the IPTC Video Metadata Hub

For video, the equivalent semantic foundation should be the **IPTC Video Metadata Hub**.

This is particularly exciting because IPTC explicitly describes the Video Metadata Hub as a **universal metadata schema** intended to provide common metadata fields across different video standards.

And importantly, IPTC says it is **not intended to invent new metadata fields**. It provides common semantics for fields already represented in other standards.

The current Recommendation is **Video Metadata Hub 1.7**, approved in October 2025.

[IPTC Video Metadata Hub](https://iptc.org/standards/video-metadata-hub/)

The Hub covers:

- descriptive metadata
- rights
- administrative information
- technical characteristics
- structured properties

and provides implementations through technologies including:

- XMP
- JSON
- EBUCore

This is almost tailor-made for the proposed library.

---

# 9. Therefore the Semantic Model Becomes Standards-Based

Rather than inventing:

    UniversalMetadata.title
    UniversalMetadata.creator
    UniversalMetadata.description

and deciding ourselves what those mean, the project would establish a standards registry:

    Metadata Property
           │
           ├── standard
           │     ├── IPTC
           │     ├── EXIF
           │     ├── XMP
           │     ├── VMH
           │     └── other
           │
           ├── canonical semantic identity
           │
           ├── definition
           │
           ├── datatype
           │
           ├── cardinality
           │
           ├── representations
           │
           └── mappings

The library's API can then expose convenient names while retaining the standards identity underneath.

For example:

    media.metadata.creator

could internally identify:

    standard = IPTC
    schema   = Core
    property = Creator

rather than simply being a project-defined string called `"creator"`.

---

# 10. Don't Force Everything Into IPTC

This is equally important.

IPTC explicitly distinguishes its descriptive/administrative metadata from technical metadata generated by cameras and other equipment. Technical metadata is often governed by other standards and manufacturers.

Therefore the project should have multiple semantic domains.

### Domain A — IPTC Photo

    Description
    Creator
    Copyright
    Credit
    Location
    Person Shown
    Subject
    Rights
    etc.

### Domain B — IPTC Video Metadata Hub

    Video description
    Creator
    Contributor
    Rights
    Technical characteristics
    Administrative information
    etc.

### Domain C — EXIF / camera technical metadata

    Camera Make
    Camera Model
    Lens
    Exposure
    Focal Length
    ISO
    Aperture
    Shutter Speed
    Orientation
    GPS
    etc.

### Domain D — Container/media technical metadata

    Duration
    Frame Rate
    Codec
    Container
    Video dimensions
    Audio streams
    Track information
    Time scale
    etc.

### Domain E — Library/application metadata

Only here should the project introduce its own definitions.

For example:

    assetId
    importedAt
    sourceFileHash
    metadataRevision
    sidecarPolicy
    metadataProvenance

Those aren't pretending to be industry-standard media metadata.

---

# 11. Canonical API vs. Canonical Storage

This distinction should be explicit.

The library should have a **canonical semantic API**, but it does not necessarily need to create a new canonical file format.

For example:

    Application

    metadata.description
    metadata.creator
    metadata.captureDate
    metadata.location

could result in:

    JPEG
     ├── EXIF
     ├── IPTC
     └── XMP

or:

    RAW
     └── XMP sidecar

or:

    MP4
     ├── embedded XMP / container metadata
     └── XMP sidecar

The application should not need to know those details.

---

# 12. Embedded vs. Sidecar Becomes a Policy Engine

The library should determine how metadata is persisted.

Example:

    metadata.write(media, policy=Preferred)

The library evaluates:

    What format is this?

    What metadata can it safely store?

    Which representations are available?

    Is a sidecar preferable?

    Does this operation risk damaging metadata?

    Does this metadata have a reliable embedded representation?

and returns something such as:

    StorageDecision
        method: Embedded
        format: XMP + EXIF
        backend: Exiv2

or:

    StorageDecision
        method: Sidecar
        format: XMP
        backend: ExifTool

The application shouldn't need to contain hundreds of format-specific rules.

---

# 13. Exiv2 and ExifTool Become Backends

The project should explicitly avoid becoming another metadata parser.

Instead:

                        API
                         │
                 Metadata Engine
                         │
                 Backend Manager
                  /            \
                 /              \
             Exiv2            ExifTool

### Exiv2

Useful particularly as a native C++ library with direct access to EXIF, IPTC and XMP structures, including XMP sidecars.

[Exiv2 documentation](https://exiv2.org/doc/)

### ExifTool

Useful as a broad compatibility/reference backend, especially where format coverage or vendor-specific metadata is important.

The architecture should allow:

    read()
        → choose backend

    write()
        → choose backend

    verify()
        → optionally use another backend

This could eventually allow the library to use one backend as the primary writer and another as an independent verification reader.

---

# 14. Capability Discovery Is a Major Feature

An application should be able to ask:

    capabilities(media)

and receive:

    Format: CR3

    Read:
        EXIF       yes
        XMP        yes
        IPTC       partial
        GPS        yes

    Write:
        EXIF       limited
        XMP        yes
        IPTC       no
        GPS        via XMP

    Sidecar:
        XMP        recommended

    Preferred backend:
        ExifTool

This is much more useful than a static:

    if extension == ".cr3"

table embedded inside every application.

---

# 15. Metadata Reconciliation

This is where the old Metadata Working Group work becomes useful.

A file may contain:

    EXIF
        DateTimeOriginal = A

    XMP
        CreateDate = B

    IPTC
        DateCreated = C

The library needs a defined reconciliation strategy.

It should incorporate:

- IPTC mapping guidance
- existing XMP conventions
- Metadata Working Group compatibility rules where applicable
- backend-specific behavior

rather than inventing arbitrary precedence rules.

This becomes:

    read raw metadata
            ↓
    identify equivalent properties
            ↓
    reconcile
            ↓
    canonical semantic representation
            ↓
    application

and on write:

    canonical metadata
            ↓
    mapping engine
            ↓
    XMP / EXIF / IPTC
            ↓
    synchronized representations

---

# 16. GPS Tracks Should Be a Separate Layer

This is one area where the project can provide functionality above the standards.

GPS track data can be treated as an external dataset:

    GPX
    NMEA
    KML
    TCX
    CSV

Then:

    Media
      │
      │ capture timestamp
      ▼
    GPS Track
      │
      │ temporal correlation/interpolation
      ▼
    Location

The resulting location can then be written using established metadata representations.

The project therefore isn't inventing a new definition for:

    latitude
    longitude
    altitude
    GPS time

It is providing a **higher-level operation**:

> "Determine the location of this media item from this track."

That is an important distinction.

---

# 17. Provenance Should Be First-Class

Because multiple standards can represent the same concept, the library should optionally retain provenance.

Example:

    metadata.captureTime

    value:
        2026-07-14T18:32:11-06:00

    sources:
        EXIF.DateTimeOriginal
        XMP.photoshop.DateCreated

    resolution:
        equivalent

    confidence:
        exact

Or:

    sources:
        XMP.CreateDate
        QuickTime.CreateDate

    resolution:
        conflict

    preferred:
        XMP.CreateDate

This would make the library much more useful for archival/DAM applications.

---

# 18. Raw Metadata Must Always Remain Accessible

The semantic API should never prevent access to unusual metadata.

For example:

    metadata.creator

is the friendly abstraction.

But applications should also be able to say:

    raw.get("Exif.Nikon3.LensType")

or:

    raw.xmp("some.vendor.namespace", "SomeProperty")

This is critical because cameras continually introduce manufacturer-specific metadata.

The philosophy should be:

> **Standardized metadata gets standardized semantics. Everything else remains accessible without forcing it into a fake universal definition.**

---

# 19. Suggested API Layers

A clean architecture might be:

    Universal Metadata Library
    │
    ├── metadata-core
    │   ├── semantic model
    │   ├── property registry
    │   ├── values/types
    │   └── provenance
    │
    ├── standards
    │   ├── IPTC Photo
    │   ├── IPTC Extension
    │   ├── IPTC Video Metadata Hub
    │   ├── EXIF
    │   └── XMP
    │
    ├── mappings
    │   ├── IPTC ↔ XMP
    │   ├── IPTC ↔ EXIF
    │   ├── EXIF ↔ XMP
    │   └── VMH ↔ video formats
    │
    ├── persistence
    │   ├── embedded
    │   ├── XMP sidecar
    │   └── other sidecars
    │
    ├── backends
    │   ├── Exiv2
    │   └── ExifTool
    │
    ├── media
    │   ├── image
    │   ├── video
    │   └── audio
    │
    └── geotagging
        ├── GPX
        ├── NMEA
        ├── KML
        └── track matching

---

# 20. Standards Registry

This should probably become one of the project's most important internal components.

For every property:

    Property
    ├── stable library identifier
    ├── standard
    ├── standard version
    ├── schema
    ├── standard property name
    ├── definition
    ├── datatype
    ├── cardinality
    ├── XMP representation
    ├── EXIF representation
    ├── IPTC representation
    ├── video representation
    └── mapping notes

Example:

    iptc.photo.creator

    standard:
        IPTC Photo Metadata

    schema:
        Core 1.5

    standardProperty:
        Creator

    representations:
        XMP:
            dc:creator

        IPTC/IIM:
            ...

        EXIF:
            ...

    source:
        IPTC Photo Metadata Standard 2025.1

The library can then generate documentation from this registry.

---

# 21. Versioning Strategy

The project should **version its implementation independently from the standards**.

For example:

    Library 1.4

    Supported standards:

    IPTC Photo Metadata:
        2025.1

    IPTC Video Metadata Hub:
        1.7

    XMP:
        ISO XMP / supported implementation

    EXIF:
        supported revision

    Exiv2:
        supported versions

    ExifTool:
        supported versions

This is important because standards will evolve independently.

An application should be able to ask:

    which standards do you implement?

rather than assuming the library's own version tells it everything.

---

# 22. Adoption Strategy

The project should explicitly position itself as:

### Not:

> A new universal metadata standard.

### Not:

> A replacement for IPTC.

### Not:

> A replacement for XMP.

### Not:

> A replacement for Exiv2 or ExifTool.

### Instead:

> **A standards-based interoperability and application API for media metadata.**

That distinction makes the project much easier to explain to potential contributors and users.

---

# 23. The "Don't Reinvent This" Rule

The project should have an explicit policy:

### If an authoritative standard exists:

**Adopt it.**

### If multiple standards describe the same concept:

**Map them.**

### If no standard exists:

**Use the most appropriate existing vocabulary.**

### Only if no suitable vocabulary exists:

**Create a project-specific property.**

And project-specific properties should be explicitly namespaced:

    umml:assetId
    umml:metadataProvenance
    umml:sourceHash

rather than pretending they are IPTC/EXIF properties.

---

# 24. Initial Standard Adoption

I would start with these:

| Area | Authority |
|---|---|
| Photo descriptive metadata | IPTC Photo Metadata 2025.1 |
| Photo rights metadata | IPTC Photo Metadata 2025.1 |
| Photo XMP representation | IPTC's specified XMP mappings |
| Photo ↔ EXIF mapping | IPTC Mapping Guidelines |
| Video semantic metadata | IPTC Video Metadata Hub 1.7 |
| Video XMP representation | IPTC VMH |
| Video JSON representation | IPTC VMH |
| Camera technical metadata | EXIF / manufacturer standards |
| General XMP framework | XMP / ISO 16684-1 |
| Metadata reconciliation | IPTC mappings + applicable MWG guidance |
| File-format implementation | Exiv2 / ExifTool |
| GPS track formats | Existing GPX/NMEA/etc. standards |

This gives the project a strong standards foundation without requiring the project to invent its own vocabulary.

---

# 25. Phase 1 — Photo Metadata

Start small.

Support:

    JPEG
    TIFF
    PNG
    WebP
    common RAW formats
    XMP sidecars

Implement:

    read()
    write()
    capabilities()

with:

    IPTC Core
    IPTC Extension
    XMP
    EXIF

The first goal isn't to support every camera.

It is to prove:

    JPEG
      ↓
    read
      ↓
    canonical metadata
      ↓
    modify
      ↓
    write
      ↓
    JPEG

while preserving correct IPTC/XMP/EXIF relationships.

---

# 26. Phase 2 — Standards Registry + Mapping Engine

Move the IPTC machine-readable reference into the project's build process.

Potentially:

    IPTC JSON/YAML
           ↓
       generator
           ↓
      property registry
           ↓
    generated code/docs/tests

Then add automated mapping tests.

This is where the project begins to become genuinely reusable infrastructure rather than merely an API wrapper.

---

# 27. Phase 3 — Video

Add:

    MP4
    MOV
    M4V
    MKV
    WebM
    MXF
    etc.

and adopt IPTC Video Metadata Hub 1.7 as the semantic basis where applicable.

The VMH is particularly valuable because its properties are explicitly designed to be represented across different video standards.

---

# 28. Phase 4 — Sidecars and Synchronization

Implement:

    embedded preferred
    sidecar preferred
    sidecar required

and:

    detectConflict()
    merge()
    synchronize()

The library should be able to recognize:

    media.jpg
    media.xmp

as one metadata-bearing media asset.

Exiv2 already has explicit XMP-sidecar support, including reading and writing XMP sidecars, which gives the project a useful low-level capability to build upon.

---

# 29. Phase 5 — GPS Track Engine

Add:

    import GPX
    import NMEA
    import KML

then:

    match(media, track)

with:

    nearest point
    interpolation
    time offset
    accuracy

The resulting location is then written through the normal metadata engine.

This is an example of functionality the library can provide **above** the underlying metadata standards.

---

# 30. Phase 6 — Verification and Preservation

A particularly interesting advanced feature would be:

    read with Exiv2
            ↓
    canonical representation
            ↓
    write with ExifTool
            ↓
    read again with Exiv2
            ↓
    compare

or the reverse.

This could provide a compatibility test suite covering thousands of real-world files.

Over time, the project's test corpus could become one of its greatest assets.

---

# 31. Relationship to Pimio

Pimio then becomes an excellent first consumer rather than the reason the library exists.

                    Universal Metadata Library
                       /              \
                      /                \
                  Pimio              Other apps
                    │
                    ▼
                   Lore

Pimio would be responsible for:

    asset identity
    media relationships
    versions
    library organization
    Lore integration
    application-specific metadata

The metadata library would be responsible for:

    EXIF
    IPTC
    XMP
    video metadata
    metadata mappings
    sidecars
    embedded metadata
    GPS geotagging
    metadata synchronization

That separation is very clean.

---

# 32. The Project's Most Important Promise

The project's central promise could eventually be stated very simply:

> **Write metadata once. Use it everywhere.**

An application developer shouldn't have to understand:

    EXIF
    IPTC IIM
    IPTC Core
    IPTC Extension
    XMP
    QuickTime
    MP4
    MXF
    camera MakerNotes
    XMP sidecars
    MWG reconciliation

just to implement:

    getDescription()
    getCreator()
    getCaptureTime()
    getLocation()
    setDescription()
    setRating()
    setLocation()

The library absorbs that complexity while relying on established standards for the underlying meanings.

---

# 33. Recommended Project Identity

Avoid names containing:

- IPTC
- XMP
- Exif
- Exiv2
- ExifTool

because the project is broader than any of those.

A working description could be:

**Universal Media Metadata Library**

or:

**Universal Media Metadata Layer (UMML)**

with the tagline:

> **A standards-based metadata abstraction layer for photos, video, and media.**

The README could immediately state:

> UMML does not define a new metadata standard. It provides a unified programming interface over established standards including IPTC Photo Metadata, IPTC Video Metadata Hub, XMP, EXIF, and media-container metadata, using proven implementation engines such as Exiv2 and ExifTool.

---

# 34. The Key Architectural Insight

The project should have **three different things that must never be confused**:

    1. SEMANTICS
       "What does Creator mean?"
                  │
                  ▼
              IPTC / other standards


    2. REPRESENTATION
       "How is Creator encoded?"
                  │
                  ▼
           XMP / EXIF / container


    3. IMPLEMENTATION
       "How do I actually read/write it?"
                  │
                  ▼
           Exiv2 / ExifTool / etc.

Your library sits across all three:

                         APPLICATION
                              │
                              ▼
                     ┌────────────────┐
                     │  UMML API      │
                     └────────────────┘
                              │
                       Semantic layer
                              │
                   ┌──────────┴──────────┐
                   │                     │
                 IPTC                   VMH
                   │                     │
                   └──────────┬──────────┘
                              │
                      Mapping layer
                              │
                   ┌──────────┴──────────┐
                   │                     │
                  XMP                  EXIF
                   │                     │
                   └──────────┬──────────┘
                              │
                       Backend layer
                         /          \
                     Exiv2        ExifTool
                         \          /
                          \        /
                            MEDIA

**That separation is what makes the project genuinely interesting.**

And the first project milestone can be:

> **Import IPTC's existing vocabulary and expose it as a typed API, rather than designing a metadata model from scratch.**

That gives the project a standards-based foundation from day one.