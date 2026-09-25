# Pimio Media Metadata Architecture

## Status

Design Proposal

## Purpose

This document defines how Pimio should handle metadata across image, RAW, and
video formats, with particular emphasis on:

- Embedded metadata
- XMP sidecars
- Metadata that cannot safely be embedded
- Exiv2 and ExifTool capabilities
- Date/time metadata
- GPS/location metadata
- GPS track/map data
- Preservation of original media
- Versioning metadata through Lore
- Portability of a Pimio library

The central design principle is:

> **Pimio owns the logical metadata model. Exiv2 and ExifTool are metadata
> interchange/implementation engines, not the authoritative metadata store.**

This allows Pimio to support media formats with very different metadata
capabilities without changing the application's underlying metadata model.

---

# 1. Design Principles

## 1.1 Original media is immutable

Pimio should treat the original imported media file as immutable.

For example:

    original/
        IMG_1234.JPG

Pimio should not require modification of the original file simply because
metadata needs to change.

Instead, metadata changes are represented separately and can optionally be
embedded into a derivative copy when appropriate.

This provides:

- Original-file preservation
- Reliable version history
- Safer handling of RAW files
- Consistent behavior across image and video formats
- Easy restoration from Lore
- Freedom to change metadata tooling later

---

# 2. Pimio's Canonical Metadata Model

Pimio should maintain a format-independent metadata representation.

Conceptually:

                    Pimio Metadata
                          |
              +-----------+-----------+
              |                       |
        Embedded form            Sidecar form
              |                       |
       +------+------+          +-----+-----+
       |      |      |          |           |
      EXIF   XMP    IPTC       XMP       Pimio-specific
       |      |      |          metadata
       +------+------+          |
              |                  |
          Media file         .xmp sidecar

The Pimio metadata model is authoritative.

EXIF, IPTC, XMP, QuickTime metadata, etc. are representations of that model.

---

# 3. Metadata Representation Layers

Pimio should conceptually distinguish three layers.

## 3.1 Canonical metadata

The metadata Pimio understands and exposes to the user.

Examples:

- Capture date/time
- Time-zone offset
- GPS latitude
- GPS longitude
- GPS altitude
- GPS direction
- Description
- Title
- Rating
- Keywords
- People
- Places
- Camera information
- Lens information
- Copyright
- Creator
- User-defined metadata

This layer is independent of the physical media format.

---

## 3.2 Embedded metadata

Metadata physically stored inside the media file.

Examples:

- JPEG EXIF
- JPEG XMP
- TIFF EXIF
- PNG XMP
- HEIC EXIF
- MP4/QuickTime metadata
- RAW metadata
- WebP EXIF/XMP

Embedded metadata should be considered a representation of Pimio metadata,
not necessarily the authoritative copy.

---

## 3.3 Sidecar metadata

Metadata stored separately from the media.

Typical example:

    IMG_1234.RAF
    IMG_1234.xmp

Sidecars are particularly useful when:

- The media format cannot store the desired metadata.
- The metadata library cannot safely write the format.
- The original file should never be modified.
- A RAW format is being preserved.
- A video container has incomplete metadata-writing support.
- Pimio-specific metadata has no standardized embedded representation.

---

# 4. Metadata Backend Strategy

Pimio should support both Exiv2 and ExifTool.

## 4.1 Exiv2

Primary characteristics:

- Native C++ library
- Very useful for common image metadata
- Strong EXIF/IPTC/XMP support
- Good RAW-image support
- Some video/container metadata support
- Lightweight compared with invoking ExifTool
- Appropriate for normal image metadata operations

Exiv2 should be considered the preferred low-level image metadata library
when it can perform the required operation safely.

---

## 4.2 ExifTool

Primary characteristics:

- Extremely broad file-format support
- Extensive RAW metadata support
- Extensive video/container metadata support
- Strong QuickTime/MP4 metadata handling
- Strong GPS/geotagging capabilities
- GPX/NMEA/KML and other track-log processing
- Large number of vendor-specific metadata tags
- Strong metadata conversion capabilities

ExifTool should be available as a compatibility/fallback metadata engine.

---

## 4.3 Backend selection

Pimio should not expose "Exiv2 mode" or "ExifTool mode" to users.

Instead:

    Pimio operation
          |
          v
    Metadata abstraction layer
          |
          +---- Exiv2
          |
          +---- ExifTool
          |
          +---- Pimio sidecar
          |
          +---- Future backend

The metadata abstraction layer determines how the operation should be
performed.

---

# 5. Image Format Support Matrix

The following matrix describes the intended Pimio design classification.

Legend:

- **R/W** = read and write metadata
- **R** = read metadata
- **Limited** = partial or format/build-dependent support
- **Sidecar** = sidecar may be required or preferred
- **ExifTool** = ExifTool generally provides broader support

| Format | Exiv2 | ExifTool | Typical Embedded Metadata | Pimio Strategy |
|---|---|---|---|---|
| JPEG/JPG | R/W | R/W | EXIF, IPTC, XMP, ICC | Embed + canonical metadata |
| TIFF | R/W | R/W | EXIF, IPTC, XMP, ICC | Embed + canonical metadata |
| BigTIFF | Limited | R/W | EXIF/XMP/etc. | Sidecar fallback |
| DNG | R/W | R/W | EXIF, XMP, IPTC, ICC | Embed + preserve original |
| CR2 | R/W | R/W | EXIF, IPTC, XMP, Canon metadata | Embed cautiously |
| CR3 | Limited/BMFF | R/W | EXIF, XMP, Canon metadata | ExifTool/sidecar fallback |
| CRW | R/W | R/W | Canon CIFF/EXIF | Preserve + metadata layer |
| NEF | R/W | R/W | EXIF, XMP, Nikon metadata | Embed cautiously |
| NRW | Limited | R/W | EXIF/XMP | Sidecar fallback |
| ARW | R/W | R/W | EXIF, XMP, Sony metadata | Embed cautiously |
| SR2 | Read-oriented | R/W | EXIF/XMP/Sony metadata | Sidecar |
| SRW | R/W | R/W | EXIF/XMP | Embed cautiously |
| RAF | Read-oriented | R/W | EXIF/XMP/Fuji metadata | Sidecar preferred |
| RW2 | Read-oriented | R/W | EXIF/XMP/Panasonic metadata | Sidecar preferred |
| ORF | R/W | R/W | EXIF/XMP/Olympus metadata | Embed cautiously |
| PEF | R/W | R/W | EXIF/XMP/Pentax metadata | Embed cautiously |
| MRW | Read-oriented | R/W | EXIF/Minolta metadata | Sidecar |
| PSD | R/W | R/W | EXIF/XMP/IPTC | Embed |
| PSB | Limited | R/W | EXIF/XMP/etc. | Sidecar fallback |
| PNG | R/W | R/W | XMP/EXIF/ICC | Embed |
| WebP | R/W | R/W | EXIF/XMP/ICC | Embed |
| HEIF | Limited/BMFF | R/W | EXIF/XMP/ICC | ExifTool/sidecar fallback |
| HEIC | Limited/BMFF | R/W | EXIF/XMP/ICC | ExifTool/sidecar fallback |
| AVIF | Limited/BMFF | R/W | EXIF/XMP | ExifTool/sidecar fallback |
| JPEG 2000 / JP2 | R/W | R/W | EXIF/IPTC/XMP/ICC | Embed |
| GIF | Minimal | R/W/limited | Limited | Sidecar preferred |
| BMP | Minimal | Limited | Minimal | Sidecar |
| TGA | Minimal | Limited | Minimal | Sidecar |
| EPS | Limited | R/W | XMP | Embed/sidecar |
| XMP | N/A | R/W | XMP | Native sidecar |

> The exact capabilities of Exiv2 can vary by version and build configuration.
> Pimio should therefore determine capabilities at runtime rather than
> hard-code every operation as universally available.

---

# 6. Video Format Support Matrix

Video should be treated separately from still images.

Video containers have significantly different metadata models and often
contain metadata at multiple levels:

- File/container
- Movie
- Track
- Stream
- Camera/vendor-specific metadata

| Video / Container | Exiv2 | ExifTool | Pimio Strategy |
|---|---|---|---|
| MOV / QuickTime | Limited/RW depending metadata | Broad R/W | Embedded when verified, sidecar fallback |
| MP4 | Limited/RW depending metadata | Broad R/W | Embedded when verified, sidecar fallback |
| M4V | Limited | Broad | Embedded/sidecar |
| M4A | Limited | Broad | Embedded/sidecar |
| F4V | Limited | Broad | Embedded/sidecar |
| MKV | Limited/RW | Broad | Sidecar preferred for Pimio metadata |
| MKA | Limited | Broad | Sidecar preferred |
| AVI | Limited | Broad | Sidecar preferred |
| WAV | Limited | Broad | Sidecar preferred |
| ASF | Limited | Broad | Sidecar preferred |
| WMV | Limited | Broad | Sidecar preferred |
| WebM | Limited | Broad | Sidecar preferred |
| FLV | Limited | Broad | Sidecar preferred |
| MPEG/MPG | Limited | Broad | Sidecar preferred |
| MXF | Minimal | Broad/read-oriented | Sidecar |
| 3GP | Limited | Broad | Embedded/sidecar |
| 3G2 | Limited | Broad | Embedded/sidecar |

The important design decision is that Pimio should **not require every video
format to support embedded metadata**.

For example:

    video.mp4
    video.mp4.xmp

is a perfectly valid Pimio representation.

---

# 7. RAW Image Strategy

RAW images require special treatment.

Pimio should assume that the RAW file is the original camera artifact.

Examples:

    IMG_0001.CR3
    IMG_0002.NEF
    IMG_0003.ARW
    IMG_0004.RAF

Pimio should not require modification of these files.

Instead:

    IMG_0001.CR3
    IMG_0001.xmp

The XMP file can contain Pimio-managed metadata.

This has several advantages:

- Original RAW remains byte-for-byte preserved.
- Camera-specific structures are not accidentally damaged.
- Metadata can be edited without rewriting RAW data.
- Lore can version metadata independently.
- A future metadata engine can be substituted without changing the original.

---

# 8. Sidecar Policy

Pimio should support three sidecar policies.

## 8.1 Required sidecar

Used when embedded metadata is impossible or unsupported.

Example:

    media.xyz
    media.xyz.xmp

---

## 8.2 Preferred sidecar

Used when embedded metadata is technically possible but modifying the
original file is undesirable.

Examples:

- RAW files
- Some video formats
- Formats with fragile vendor metadata
- User-selected "preserve originals" mode

---

## 8.3 Embedded preferred

Used when the format has mature and reliable embedded metadata support.

Examples:

- JPEG
- TIFF
- PNG
- WebP
- DNG

Even here, the canonical Pimio metadata remains logically independent of
the embedded representation.

---

# 9. Sidecar Naming

Recommended convention:

    filename.ext
    filename.xmp

Examples:

    IMG_1234.JPG
    IMG_1234.xmp

    IMG_1234.NEF
    IMG_1234.xmp

    Vacation.mp4
    Vacation.xmp

An alternative is:

    IMG_1234.NEF.xmp

Pimio should select one convention and use it consistently.

The sidecar association should additionally be stored in Pimio's internal
metadata database rather than relying solely on filenames.

---

# 10. Date and Time Model

Pimio should not expose a single generic "date" internally.

At minimum, the canonical model should distinguish:

    captureDateTime
    captureTimeZone
    digitizedDateTime
    fileCreatedDateTime
    fileModifiedDateTime

For video, additional container/track timestamps may exist.

Example:

    captureDateTime:
        2026-09-23T14:32:18.123

    captureTimeZone:
        -06:00

    fileCreatedDateTime:
        ...

    fileModifiedDateTime:
        ...

Pimio should preserve source metadata when multiple timestamps exist rather
than silently collapsing them.

---

# 11. Timestamp Normalization

Different formats can express time differently.

Examples include:

- EXIF DateTimeOriginal
- EXIF OffsetTimeOriginal
- QuickTime CreateDate
- QuickTime ModifyDate
- MP4 timestamps
- filesystem creation time
- filesystem modification time
- GPS timestamps
- XMP CreateDate
- XMP ModifyDate

Pimio should normalize these into its canonical representation while
retaining the original source fields when useful.

Conceptually:

    Source metadata
          |
          v
    Metadata parser
          |
          v
    Canonical Pimio timestamp
          |
          +---- original EXIF value
          +---- original XMP value
          +---- original QuickTime value
          +---- source/timezone information

This prevents loss of information during import.

---

# 12. GPS / Location Model

Pimio should distinguish geographic metadata from geographic tracks.

## 12.1 Media location

Canonical fields should include at least:

    latitude
    longitude
    altitude

Optional fields:

    direction
    speed
    horizontalAccuracy
    verticalAccuracy
    GPSDateTime

Potential human-readable fields:

    city
    state/province
    country
    countryCode
    locationName

---

# 13. GPS Track Model

A GPS track is not simply metadata belonging to one photograph.

It is an independent geographic/time dataset.

Example:

    Trip/
        track.gpx

The track can contain:

    timestamp
    latitude
    longitude
    altitude
    speed
    direction

Pimio should be able to associate media with the track using capture time.

Conceptually:

    GPS Track
        |
        | timestamp matching
        v
    Media capture time
        |
        v
    Interpolated position
        |
        v
    Pimio canonical location
        |
        +---- embedded GPS
        |
        +---- XMP sidecar
        |
        +---- Pimio metadata

---

# 14. Map / GPS Track Files

Pimio should consider these files as first-class importable metadata sources:

| Format | Purpose | Pimio Role |
|---|---|---|
| GPX | GPS tracks/routes/waypoints | Importable track |
| KML | Geographic data | Importable location/track |
| KMZ | Compressed KML | Importable location/track |
| NMEA | GPS receiver logs | Importable track |
| TCX | Garmin activity data | Importable track |
| IGC | Aviation/GPS track | Importable track |
| CSV GPS logs | Generic track data | Importable track |
| Google Takeout location data | Location history | Importable location source |

ExifTool has substantially more built-in functionality for GPS-track-based
geotagging than Exiv2.

Pimio should therefore expose track processing as a higher-level Pimio
operation rather than treating it as an Exiv2 operation.

---

# 15. Geotagging Architecture

Recommended architecture:

    GPS Track
       |
       v
    Pimio Track Parser
       |
       v
    Time/position matching
       |
       v
    Pimio Location
       |
       +----------------+
       |                |
       v                v
    Embedded          XMP sidecar
    metadata
       |
       v
    Media file

ExifTool may be used as the implementation engine for portions of this
operation, but the resulting location should become canonical Pimio metadata.

---

# 16. Metadata Import

When media enters Pimio:

    Media file
       |
       v
    Format detection
       |
       +--------------------+
       |                    |
       v                    v
    Exiv2                ExifTool
       |                    |
       +---------+----------+
                 |
                 v
         Metadata normalization
                 |
                 v
         Pimio canonical metadata
                 |
                 v
         Lore versioned state

Pimio should retain provenance where practical.

Example:

    metadata.captureDateTime.source = EXIF:DateTimeOriginal
    metadata.location.source = XMP:GPSLatitude/GPSLongitude

---

# 17. Metadata Editing

When a user changes metadata:

    User edit
       |
       v
    Pimio canonical metadata
       |
       +-------------------+
       |                   |
       v                   v
    Embed              Sidecar
       |                   |
       v                   v
    Exiv2              XMP writer
       |
       or
       |
    ExifTool
       |
       v
    Updated representation
       |
       v
    Lore commit

The original media should remain recoverable.

---

# 18. Metadata Synchronization

Pimio should have a synchronization process between:

    Canonical Pimio metadata
            |
            +---- Embedded metadata
            |
            +---- XMP sidecar

This is important because users may modify metadata outside Pimio.

For example:

    Pimio
       |
       v
    IMG_1234.JPG

User then edits metadata using another application.

On next import/synchronization Pimio should detect:

- Embedded metadata changed
- Sidecar changed
- Pimio metadata changed
- Conflicting changes

and present the conflict to the metadata synchronization layer.

---

# 19. Metadata Conflict Handling

Example:

    Pimio:
        Rating = 4

    XMP sidecar:
        Rating = 5

Pimio should not silently choose one.

The system should record:

    Metadata conflict
        Field: Rating
        Pimio value: 4
        Sidecar value: 5
        Source: external modification

Possible resolution:

    Keep Pimio
    Keep external
    Merge
    Create new metadata revision

Because Lore provides versioning, metadata conflicts can be safely
represented as historical states rather than destructive changes.

---

# 20. Lore Integration

The metadata architecture should integrate naturally with Lore.

Conceptually:

    Pimio Library
          |
          v
       Lore Repo
          |
      +---+------------------+
      |                      |
      v                      v
    Media                 Metadata
      |                      |
      +----------+-----------+
                 |
                 v
             Versions

A Lore commit might contain:

    media/
        original/
            IMG_1234.JPG

    metadata/
        IMG_1234.xmp

    pimio/
        metadata.json

    derived/
        thumbnails/
            IMG_1234.jpg

---

# 21. Recommended Lore Object Structure

A conceptual Pimio object:

    media/
        <media-id>/
            original/
                original-file.ext

            metadata/
                metadata.xmp

            derived/
                thumbnail.jpg
                preview.jpg

            pimio/
                metadata.json

The exact physical layout may change during implementation.

The important distinction is between:

1. Original media
2. Canonical Pimio metadata
3. External-standard metadata representation
4. Derived media

---

# 22. Canonical Metadata vs XMP

Pimio should not make XMP its complete internal metadata database.

Instead:

    Pimio canonical metadata
             |
       +-----+------+
       |            |
       v            v
      XMP        Pimio-specific
                   metadata

XMP should be used whenever possible because it provides a portable,
industry-recognized representation.

However, Pimio may have information that does not have a suitable standard
XMP representation.

Examples could include:

- Pimio version history identifiers
- Pimio-specific relationships
- Library-specific organizational metadata
- Internal asset IDs
- Lore object identifiers
- Derived-media relationships

That information belongs in the Pimio metadata layer.

---

# 23. Original vs Derivative Files

Pimio should distinguish:

    Original
       |
       +---- Metadata representation
       |
       +---- Thumbnail
       |
       +---- Preview
       |
       +---- Edited image
       |
       +---- Trimmed video
       |
       +---- Transcoded video

Metadata should be associated with the logical Pimio asset and, where
appropriate, inherited by derivatives.

For example:

    Original video
         |
         +---- Trimmed version
         |
         +---- Preview version

The trimmed video should not necessarily become the new "original."

---

# 24. Video-Specific Metadata

Video metadata should support at least:

- Capture date/time
- Time-zone information
- GPS location
- Duration
- Frame rate
- Video dimensions
- Orientation
- Camera/device
- Codec
- Container
- Audio metadata
- Creation/modification timestamps

Pimio should preserve container-specific metadata where practical but
normalize commonly useful values into the canonical Pimio model.

---

# 25. Video Metadata and Sidecars

Because video metadata support varies substantially between containers,
Pimio should allow:

    video.mp4
    video.xmp

without treating the sidecar as an error or degraded state.

For video, sidecars should be considered a normal supported architecture.

The UI may optionally expose:

    Metadata storage:
        Embedded
        Sidecar
        Both

Internally, however, the canonical Pimio metadata remains independent.

---

# 26. Embedded Metadata Safety Levels

Pimio should assign a capability level to every supported format.

## Level A — Safe embedded metadata

Example:

    JPEG
    TIFF
    PNG
    WebP

Normal metadata embedding is supported.

---

## Level B — Embedded metadata with format-specific risks

Example:

    RAW
    complex camera formats

Pimio may support embedding but should generally prefer sidecars for
user-authored metadata.

---

## Level C — Limited embedded metadata

Example:

    Some video containers
    Less common image formats

Pimio should use sidecars unless a specific operation is known to be safe.

---

## Level D — No useful embedded metadata

Pimio must use a sidecar or its internal metadata representation.

---

# 27. Runtime Capability Detection

Pimio should avoid assuming:

    "Format X always supports operation Y."

Instead, the metadata abstraction layer should query capabilities.

Example:

    MetadataCapabilities
        canReadExif
        canWriteExif
        canReadXmp
        canWriteXmp
        canWriteEmbeddedMetadata
        supportsGps
        supportsVideoMetadata
        supportsSidecar
        supportsTimestamp
        supportsMakerNotes

This accommodates differences between library versions and builds.

---

# 28. Metadata Backend Decision Process

Conceptually:

    Requested metadata operation
              |
              v
       Is embedded safe?
          /        \
        Yes         No
         |           |
         v           v
    Can Exiv2      Sidecar
    perform it?       |
      /    \          |
    Yes     No        |
     |       |        |
     v       v        |
   Exiv2   ExifTool   |
     |       |        |
     +-------+--------+
             |
             v
       Pimio metadata
             |
             v
         Lore commit

The exact implementation may differ, but the architectural concept should
remain.

---

# 29. ExifTool's Special Role

ExifTool should be considered Pimio's broad compatibility engine.

It is particularly valuable for:

- RAW formats
- Video containers
- Vendor-specific metadata
- QuickTime/MP4 metadata
- GPS track processing
- Metadata conversion
- Unusual camera formats
- Metadata formats not directly supported by Exiv2

ExifTool should not, however, become the Pimio data model.

---

# 30. Exiv2's Special Role

Exiv2 should be considered Pimio's efficient native image metadata engine.

It is particularly useful for:

- JPEG
- TIFF
- DNG
- Common RAW formats
- EXIF
- IPTC
- XMP
- ICC-related image metadata
- Fast metadata inspection

This allows common operations to remain lightweight without requiring
ExifTool for every file.

---

# 31. Long-Term Extensibility

The architecture should permit future metadata engines.

For example:

    Pimio Metadata API
           |
    +------+------+------+
    |      |      |      |
  Exiv2 ExifTool FFmpeg  Future
                         backend

This prevents Pimio from becoming tightly coupled to one metadata library.

FFmpeg could potentially become useful for media-container inspection or
transcoding-related workflows, while ExifTool remains the more comprehensive
metadata engine.

---

# 32. Recommended Initial Implementation

### Phase 1

Implement canonical Pimio metadata model:

- Capture date/time
- Time zone
- GPS latitude
- GPS longitude
- GPS altitude
- Title
- Description
- Keywords
- Rating
- Creator
- Camera/device information

Implement:

- JPEG
- TIFF
- PNG
- WebP
- common RAW formats

Use:

- Exiv2 for primary image operations
- XMP sidecars for RAW/original preservation

---

### Phase 2

Add:

- ExifTool integration
- Expanded RAW support
- HEIF/HEIC
- AVIF
- CR3
- broader camera metadata
- vendor-specific metadata preservation

---

### Phase 3

Add video:

- MOV
- MP4
- M4V
- MKV
- WebM
- AVI
- other common containers

Implement:

- canonical video timestamps
- GPS metadata
- video metadata extraction
- XMP sidecars
- container metadata where safe

---

### Phase 4

Add geographic intelligence:

- GPX import
- GPS track visualization
- track/media matching
- timestamp-based geotagging
- location interpolation
- location metadata synchronization

ExifTool can be used as an implementation component for portions of this
functionality.

---

# 33. Recommended High-Level Architecture

The resulting architecture should look approximately like this:

                            +----------------------+
                            |       Pimio UI       |
                            +----------+-----------+
                                       |
                                       v
                            +----------------------+
                            | Pimio Metadata Model |
                            +----------+-----------+
                                       |
                         +-------------+-------------+
                         |                           |
                         v                           v
                +----------------+          +----------------+
                | Metadata       |          | GPS / Track   |
                | Abstraction    |          | Engine        |
                +-------+--------+          +-------+--------+
                        |                           |
             +----------+----------+                |
             |                     |                |
             v                     v                |
         +-------+             +---------+          |
         | Exiv2 |             | ExifTool|          |
         +---+---+             +----+----+          |
             |                      |               |
             +----------+-----------+---------------+
                        |
                        v
               +-------------------+
               | Embedded Metadata |
               | / XMP Sidecar    |
               +---------+---------+
                         |
                         v
                +------------------+
                |     Media        |
                +------------------+
                         |
                         v
                +------------------+
                |       Lore       |
                | Version History  |
                +------------------+

---

# 34. Core Architectural Rule

The most important rule in this design is:

> **Pimio metadata is authoritative; embedded metadata and sidecars are
> representations of that metadata.**

This prevents the application from being constrained by the capabilities of
any particular media format or metadata library.

A JPEG, RAW file, MP4 video, MKV video, and an otherwise metadata-poor file
can all expose the same logical Pimio metadata interface.

For example:

    Asset A: JPEG
        canonical metadata
              |
              +--> embedded EXIF/XMP

    Asset B: RAW
        canonical metadata
              |
              +--> XMP sidecar

    Asset C: MP4
        canonical metadata
              |
              +--> embedded QuickTime/MP4 metadata
              |
              +--> XMP sidecar

    Asset D: MKV
        canonical metadata
              |
              +--> XMP sidecar

From Pimio's perspective, all four assets can have:

    Date
    Location
    Description
    Keywords
    Rating
    Creator

without requiring the underlying formats to implement those fields in the
same way.

---

# 35. Relationship to Lore

Lore should remain responsible for:

- Version history
- Object storage
- Repository transport
- Restoration
- Synchronization
- Deduplication/versioning as provided by Lore

Pimio should remain responsible for:

- Media library semantics
- Canonical metadata
- Metadata synchronization
- Sidecar management
- Media relationships
- Derived-media relationships
- User-facing organization

Exiv2 and ExifTool should remain responsible for:

- Parsing external metadata
- Writing external metadata
- Format-specific metadata translation

This separation keeps the Pimio architecture modular and allows the
metadata subsystem to evolve independently from the Lore storage layer.

---

# 36. Summary

Pimio should not treat media metadata as something that simply "belongs
inside the file."

Instead, metadata should be modeled as a first-class, versioned Pimio
object that can have one or more physical representations.

The preferred hierarchy is:

    1. Pimio canonical metadata
    2. Embedded EXIF/XMP/etc. when appropriate
    3. XMP sidecar when embedding is unavailable, unsafe, or undesirable
    4. Format-specific metadata preserved where possible
    5. Original media always recoverable

This provides a consistent metadata model across:

- JPEG
- TIFF
- PNG
- WebP
- HEIC/HEIF
- AVIF
- DNG
- Canon RAW
- Nikon RAW
- Sony RAW
- Fuji RAW
- Panasonic RAW
- Olympus RAW
- Pentax RAW
- MOV
- MP4
- M4V
- MKV
- WebM
- AVI
- and additional formats as support expands

It also provides a clean path for:

- GPS tracks
- map data
- automatic geotagging
- video timestamps
- RAW sidecars
- external metadata synchronization
- metadata conflict resolution
- Lore versioning
- future metadata engines

The resulting system is therefore **format-independent at the Pimio level,
while remaining format-aware at the metadata implementation level.**