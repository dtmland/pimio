# Pimio: addendum


## MCP Server

Pimio should have an MCP server with functions such as those below. This idea and this list needs more planning and development before being implemented:
- list_libraries()
- list_media(library, folder)
- get_media_metadata(id)
- get_version_history(id)
- restore_version(id, version)
- search_media(query)


## Support New File Types and Conversions


### Modern Image Formats
* **.heic / .heif**: High Efficiency Image Container. The default format for modern iOS and Android devices, offering twice the compression of JPEG at identical quality.
* **.webp**: Google's modern web image format. Widely adopted across the internet for its superior lossy and lossless compression.
* **.avif**: AV1 Image File Format. An open, royalty-free format offering even better compression than WebP and HEIC, with deep color depth support.
* **.jxl**: JPEG XL. A next-generation image format featuring ultra-high-fidelity, responsive web architecture, and lossless transcoding of legacy JPEGs.

### Modern Camera RAW Formats & Pipelines
* **.cr3**: Canon's modern RAW format. Replaced `.cr2` to introduce better compression (including C-RAW) and updated metadata structures.
* **.gpr**: GoPro RAW format. A highly compressed RAW format based on the Adobe DNG standard for action cameras.
* **Apple ProRAW / Samsung Expert RAW**: Modern computational RAW implementations. Though technically wrapped in a `.dng` extension, they include complex multi-frame, semantic, and tone-mapping metadata that a legacy Picasa engine cannot decode properly.

### Modern Video & Animation Formats
* **.mkv**: Matroska Multimedia Container. The modern standard for high-definition video files, supporting unlimited audio, video, picture, and subtitle tracks.
* **.webm**: Google-backed royalty-free container designed for the web, utilizing VP8, VP9, or AV1 video codecs.
* **.hevc / .h265**: High Efficiency Video Coding. Though often wrapped in an `.mp4` or `.mov` container, legacy Picasa cannot decode this codec, which is universally used for 4K and 8K mobile recording.
* **Animated WebP / AVIF**: Replaced legacy animated GIFs on the modern web, providing full alpha-channel transparency and superior frame compression.

### Modern Design, Vector & Vector Asset Formats
* **.svg**: Scalable Vector Graphics. The universal standard for responsive web layout graphics, icon sets, and vector illustrations.
* **.ai**: Adobe Illustrator Artwork. Modern vector asset format used broadly across digital design workflows.
* **.heics / .heifs**: High Efficiency Image Sequence. Used for storing bursts of images, live photos, or animations within an HEIF infrastructure.

## MODES
Pimio should support two different types of modes: Browser, Library

## Browser Mode

This is the standard operating mode of pimio. By default upon first-time launch (with no specicial invocation) pimio might open to the users home directory 'Pictures' folder. In browser mode pimio behaves much more like picasa did. It displays a folder hierarchy in a side bar and allows user to navigate files and directories to view images/videos from the directories in the tile view similar to how picasa did. 

This 'Browser' mode is intended to be used for adhoc pimio use where one might just want to open and view some photos/videos and perhaps modify some of them or adjust their metadata. In this mode pimio isn't copying files into any new areas or library directories - it is simply browsing the photos and videos whereever they happen to originally live. In this mode if a user uses the OS file navigator (explorer/finder/dolphin/etc) to drag and drop a folder or file onto the pimio window then pimio simply interprets this to mean the user asking pimio to open the directory or if a file then the directory in which the file lives to be opened by pimio - all of this similar to how pimio might behave if the user selected 'File->Open Directory' in the pimio menu bar. In this sense pimio never really 'opens' an individual file for viewing - it will always simply open the directory in which that file lives and then display the photo in the tile view with preview mode as if the user had clicked on it in tile view anyways. 

Pimio does all of this in browser mode without creating any lore repositories or libraries. In fact, in this mode pimio should not only behaves like picasa in the file browser sense - it should also read and create the identical picasa.ini sidecar files and '.picasaoriginals' directories just like picasa did! This preserves legacy compatibility with picasa managed media set. For more details on how picasa functioned with this 'pseudo version control process' see the later section of the same name.

In file browser mode the lore features of pimio are entirely absent and irrelevant. If pimio encounters a pimio library directory in the course of its directory navigation or heirarchy - pimio should detect this and not treat it as a standard directory whose contents should be recursed and displayed like any other standard directory in the pimio view. The 'Browser' file scanner should stop scanning at that directory because it is a library. This has some implication the the 'Browser' mode file scanner and any file scanner that library mode might have should be unique from one another in some fashion (even if they share code somehow). The library directory should sort of be displayed as an object that if the user attempts to select it open it then pimio would prompt the user asking if they would like to open the library which would then switch pimio from 'Browser' mode to 'Library' mode.

### Browser Mode - Pseudo Version Control

The legacy picasa application used a pseudo version control process with its picasa.ini files and .picasaoriginals subdirectories. In 'Browser' mode pimio should not only support reading of these files but fully adopts them as the 'primary method' of how pimio behaves when in 'Browser' mode. It will emulate the behavior of picasa in this sense both creating and writing to the ini files as needed as well as the .picasaoriginals directories. It will only write fields into the ini files that picasa knew about - such that any modifications made to a directory and all of its subdirectories could be in theory opened by picasa and it would be none the wiser! 


## Library Mode

Pimio is not in library mode by default - the user should either do one of the following to be in library mode: create a new library, or open an existing library using the 'File -> Open Library' menu. In library mode pimio no longer behaves like a file browser like the 'Browser' mode. It of course does not display a folder heiarchy in the side bar it instead displays some kind of timeline navigation widget view as the focus for pimio libraries is chronologically organized media - similar to how modern Apple Photos app behaves. After creating a new pimio library, it starts out empty and the user is instructed to add/import photos/videos whether by pointing to directories or dragging photos or direcoties onto the application. Library mode is where pimio starts to care about lore version control. It creates the lore repository as an integral part of the pimio library. It will eventually offer the ability to connect to remote lore servers for pulling down remote libraries (and eventually collaboration). Any media that is 'added/imported' into the library is of course copied into the relevant library directory to be part of the lore repository. By default libraries should be created in the some location that is not buried away from the user - pimio should created them in the standard user home directory pictures or photos folders. Pimio can of course offer the option to move the location of the library, as it is simple a directory. The user can close a library in pimio by choosing 'File -> Close Library' and then pimio simply returns to standard 'Browser' mode perhaps in the most recently open directory location by pimio. One creative idea I thought about is that if a user imports a file or directory into the library - and this is accompanied with the 'Browser' mode sidecar ini files or '.originals' folders whether at the root of the imported directory or recurisvely found throughout - pimio should detect the presense of these files and insteaed of just whole adding them into the lore repository pimio should instead translate the spirit of the ini file modifications and '.originals' into a version control of the media where the original file represents the first commit of the median and then a subsequent commit represents the modified file that sits above the '.originals' directory. In this fashion a lore repository within a pimio library should never store the ini files or the directory that housed the '.originals' - but only the original media file itself. 

Advanced image detecting and processing features
It could be that some of the advanced features like processing using opencv and modern LLM models simply prefer media formats both image and video be in modern formats - and that attempting to run processing of media on older formats would require a silly temporary file or cpu heavy streaming process to go back and forth on the media type in order to perform processing. For this reason it may be prudent to limit the pimio library type to certain media formats - and more critically limit the available special processing to those certain formats also. If special processing is limited to certain media types it might be prudent to limit the special processing to library mode in general? If are not going to require that restriction then at least it must somehow be communicated to the user that some media types do not support special processing. Whether these media types are simply marked in some special way to signify their unsupported nature (whether legacy types or other various 'view-only' types). I sort of view three hypothetical groups of media types: 'Pimio View-Only' media (whether unknown formats, or known formats whether legacy or not - that do not support modifications for metadata etc), 'Pimio Supported' (known and supported modifications), 'Pimio Preferred' (supported types but also most commonly used for special processing, and optionally also modern high efficieny compression available).

Library mode import conversion process
While browser mode is happy to open and view all supported file types and does not require or attempt to influence the user about converting file types (thought they can choose to convert if they want but when doing this pimio should slighty nudge that they might want to try out library mode), library mode is different. Library mode wants to have media converted to modern formats in order to improve capability and compatibility for advanced features. For any given new library, pimio will have its default types enabled as types which should be used for all needed conversions on imported media. It is not yet known at the moment in the design of pimio, but if there end up being multiple types that qualify for the 'Pimio Preferred' media types then perhaps the user can be given the option to decide which 'Pimio Preferred' types the library conversion process should use instead of just using the default set of 'Pimio Preferred' types. Another implied step of the library import process is the picasa version history transposition into lore. This is explained in another section.


Conversion Conundrum in version history
With the implied ability to version control media there is a slight conundrum when it comes to a certain type of operation while working with media especially image and video: conversion from one format to another. Not only does this imply a complete non-matching jump of all binary data but also implies a unique file object should exist that represents the before and after. Nothing really surprising or unique about this, it is what just about any user would expect. However, when it comes to version tracking of media, I keep thinking that it would be nice to see this operation show up as simply another step in the history of any given file. For example if a video is converted from wmv to mp4 (often with two files now existing and in common workflows the older wmv file typically being simply deleted) - and then later the mp4 has several modifications and edits - it would be great that if instead, in pimio, in the presented history of the mp4 version of the file that the first operation was a conversion from wmv to mp4 - and that in theory the user could go all the way back to the original wmv is desired for whatever reason. In pure version control semantics this doesn't quite jive because there were two unique files and trying to represent these as part of the same linear history where one is an earlier version of the other doesn't really make sense. I suppose if the file object is not named without its typical file extension one could simply have a bare file that starts out as wmv content and then later is mp4 content and the version control process would likely be ok with that? The only other alternative is that the version contorl is actually tracking a .wmv and then later a .mp4 file and that it somehow notes that they are linked and that pimio can look for that note and perform some kind of nice representation of this fact that they should be presented in the same linear history. In general just not sure what decision should be made here.

Library Structure
As mentioned before the pimio library object is simply a directory with files and subdirectories within. At some level within there is a lore repository that keeps track of version history for the library. This lore repository durable store concept has pros and cons - great for keeping track of granular modifications to the library and the ability to revert to revisions but not so great for the ballooning storage required to accomplish this. From the users perspective the size of the pimio library would, in worst case, at times be about ~2x the size of the media which they have imported because we have the checked out copy and the repository blobs that represent the lore repository. The trade-off here is acceptable - especially when it comes to the future possibility of using a lore server and the collaboration functions that could be achieved. Biting the bullet now on high storage costs seems like an acceptable compromise.

I realize lore has some lazy load features that try to prevent having so much content required to be checked out on the client end - only what the users needs - but in our case we want the users to be able to view their entire library at any given moment so from what I understand the entire repository will need to be loaded and available for use. Obviously we will need to take additional measures to accomplish, such a looping or iterating over the lore object list in order to checkout all objects that a simple clone would not. Perhaps some future version of pimio could embrace the spirit of what lore is trying to do and be smart about not having to checkout all content immediatly for the full library and it could only checkout a comprehensive set of light-weight fast-load artifacts thumbnails (or animated gifs for videos for example), then as the users browse the library and attempt to zoom into images or open a specific images would then this would trigger the need for lore to checkout larger or full size copies of media. I think for now the more simple implementation for pimio is just the try to load the whole library as mentioned above?

Initially I had thought that by default all objects should immediately be committed into the lore repository as they are added to the library, however I am re-thinking this position but with hesitation. The benefit to adding all content to the library immediately is that eventually if the user wants to push their library to a remote lore server - pimio would already have committed all content into the local copy of the repository and the users would avoid this step of having to wait around while pimio performs this extra 'unexpected or surprising' under the hood step of what is essentially commiting bulk chunks into the lore repository all at once before their local repo can be promoted and push to a remote lore server. I guess my idea about not immediatly committing all media into the lore repository and sort of keeping them in a sister directory was driven by this idea of saving space: the idea would be that objects are only committed into the lore repository if they are to be modified in any way. All unmodified media would only live in the sister directories within the library and not in the lore repository. In fact, initially I thought I could spare users the pain having what is effectively the ~2x storage cost by having the lore repository house all media right way - I am thinking this idea of sparing users from this pain is naive because in most cases this period would be short. If one of the main purposes of pimio is to repair timestamp metadata for libraries then it is very possible that most all images, as soon as they enter the pimio library, will need some modifications - even if small - thus neccessitating them to be in the lore repository very soon anyways and thus period where storage is saved or optimized is short anyways. I am thinking of sticking with the idea that all objects should just be commited to the lore repository as soon as they are added to the library.






### The Pimio Replay & Ingestion Pipeline

TODO: This section does yet include the fact that this "replay & ingestion" process only applies specifically to importing media into a Pimio library. It is not irrelevant for 'Browser' mode. The section wording and explanation needs to be updated to account for this.

One core challenge of **Pimio** library import process is detecting the presence of and translating the 'multi-copy', 'sidecar-dependent' legacy Picasa 'Pseudo Version Control' layouts into a clean, **linear Git-like history** via embedded **Lore version control**.

To accomplish this, Pimio maps Picasa’s fragmented folder states into three discrete database milestones: the **Baseline Commit** (the past), the **Saved-Edits Commit** (the present), and the **Working Index** (the uncommitted future).

Here is the exact lifecycle of how Pimio ingests, reconstructs, and represents a legacy Picasa directory under the hood using Lore:

---

### Phase 1: State Matrix Scanning & Discovery
Before running any version control operations, Pimio recursively scans the target folder structure to categorize every media file into one of four states based on the presence of `.picasa.ini` parameters and `.picasaoriginals` pairings:

| Picasa State | Main File Context | `.picasaoriginals` Context | `.picasaini` Context | Pimio's Interpretation |
| :--- | :--- | :--- | :--- | :--- |
| **Pure Pristine** | Untouched Original | None | No edits listed | A file that has never been altered. |
| **Unsaved Edits** | Untouched Original | None | Contains active edit metadata | Edits exist only as metadata; file state is uncommitted. |
| **Saved Edits** | Modified Baked Copy | Contains True Original | Flagged as "Saved" | A historic change has been permanently written to disk. |
| **Saved + New Unsaved**| Modified Baked Copy | Contains True Original | Flagged as "Saved" + New unbaked metadata | A historic change was baked, followed by subsequent active edits. |

---

### Phase 2: Replaying History into the Lore Repository
Once every file is classified, Pimio initializes a Lore repository (`lore init`) at the root directory and executes a structured multi-pass ingestion pipeline. This process moves forward through "virtual time" to recreate a logical commit graph.

#### Pass 1: Reconstructing the Initial State (The Core Baseline)
Pimio constructs a clean, uniform baseline containing exclusively the **original, unedited versions** of all media.
1. **Targeting Originals:** Pimio queues up all **Pure Pristine** files, all **Unsaved Edits** files, and pulls the true originals out of the hidden **`.picasaoriginals`** directories for any saved entries.
2. **Staging the Past:** It copies these files into a virtual layout matching their destination paths. 
3. **Lore Commit #1:** It commits this entire collection as the primary baseline:
   ```bash
   lore commit -m "Initial baseline: Import original unedited media from Picasa"
   ```

#### Pass 2: Hard-Committing Historic Saves (The Saved State)
Pimio now steps forward to capture the modifications that the user explicitly chose to write to disk while using Picasa.
1. **Targeting Baked Changes:** Pimio locates all files classified as **Saved Edits**. It discards the cached versions inside `.picasaoriginals` and selects the modified JPEG files that were sitting in the primary parent folders.
2. **Updating the Tree:** Pimio overwrites the original files in the working directory with these modified versions.
3. **Lore Commit #2:** It creates a second commit representing the explicit actions taken in the past:
   ```bash
   lore commit -m "Picasa Save State: Commit historic modifications baked to disk"
   ```

#### Pass 3: Constructing the Modern Working Index (The Unsaved State)
Finally, Pimio brings the repository up to the exact present moment by translating unbaked Picasa metadata into an active Lore staging area.
1. **Targeting Active Metadata:** Pimio searches for any files with **Unsaved Edits** (from either Pass 1 or Pass 2).
2. **Baking On-The-Fly:** Pimio's internal rendering engine processes the file through the exact filter or crop parameters detailed in the `.picasa.ini` string.
3. **Dirtying the Index:** It writes this newly rendered image directly over the file in the working directory, but **does not invoke a commit command**.

---

### Phase 3: The UI Mapping (Representing the "Save" Button)
Once the pipeline finishes, Pimio permanently purges the legacy `.picasa.ini` files and deletes all `.picasaoriginals` directories from the disk. The user interface seamlessly links its visual states directly to Lore's file tracking:

* **The Active Interface:** When viewing the folder inside Pimio, files with unsaved edits appear modified because the image on disk is altered. 
* **The "Unsaved" Indicator:** Pimio queries the embedded client (`lore status`). If a file is flagged as modified or staged but uncommitted, a **"Save Changes"** button lights up in the Pimio UI next to that asset.
* **Clicking "Save":** When the user clicks the button, Pimio performs a native version control operation behind the scenes:
  ```bash
  lore commit -m "Pimio UI: Explicit user commit of active modifications"
  ```
  The button turns off because the working index is clean.
* **Clicking "Undo / Revert":** If the user chooses to revert their changes instead of saving, Pimio simply checks out the previous commit:
  ```bash
  lore checkout -- photo.jpg
  ```
  Lore instantly swaps the modified image back to its last committed state safely, cleanly, and without doubling your storage footprint.




## Add-Ons Manager

Pimio should feature an add-ons manager. This can support both pimio delivered add-ons and user created add-ons. One of the initial use cases for the add-on manager is to download semi-required components - artifacts that we don't want to deliver with the pimio installer but that are non-the-less required for advanced pimio operations to work. If the add-ons are not downloaded then pimio would simply represent these advanced functions as disabled - perhaps greying out any relevant UI controls and providing useful 'disabled' behavior or messages on the MCP interface. There are several motivations for the need for an add-ons manager and artifacts that are not delivered with pimio install artifacts - in most cases it will be because of file size concerns - model files for LLMs or other large models files. Another type of add-on might be an offline tile set for an offline capability for the standard pimio gps map view. Besides large files, other motivations are components that we want to have updated over time without having to rev new versions of pimio itself - the user selecting manual timezone database is a good example of this. Other possible examples for having an add-on manager is for artifacts with license restrictions that cannot be included with pimio for licensing reasons. Finally, as mentioned in the beginning, user created add-ons would also be a good use case. While the add-ons manager might by default try to download any given add directly from the internet - it should also offer the user the option to manually provide the artifact themselves for offline installations.

## Detection and Processing features

Whether supporterd by opencv or other modern model weights: pimio should have the ability to detect the date on old film based photos that had the little red date superimposed onto the bottom corner of the photo.


## GUI Layout and Function


### Custom Sorting

While the view already supports support viewing by name, date, type etc - there will also be a custom sorting mode where the sorting is user defined - user defined in the sense that the user would be able to grab a photo and drag it around in the tile array to change where it's place in the order of tiles (or photos) is. This feaure was available in picasa and while it did not refer to it like we are here as 'custom sorting mode' the idea was the same nevertheless. The user should even be able to select a group of photos and drag the entire group as a unit to change the place of ordering where the engire group fits. Picasa had this nice animation that would not only show the thumbnail tile (a translucent version of the tile actually!) that the user was dragging around but also the tiles near the area where the cursor was moving around the tile view would cause adjacent tiles to slightly shift away as if they were 'making room' for the new tile to fit in that location sort of anticpating if the user might drop the photo in the location. Not sure where picasa saved this custom ordering but again it never referred to it that it way maintined this user customized state if after picasa restarts. In picasa's case even after customizing the order to the tiles in this fashion the 'View->Folder View->Sory By' select did not change it would just stay at whatever the user had last selected. For pimio I am thinking that the user should explicitly enter a 'custom sort mode' using that the dropdown or whatever gui is used to control the sort type mode. At that point the user could then just click and drag a tile or a select a group of tiles and then grab and drag those around.

It might be that in pimio with QT we are not able to achieve quite the exact same behavior as picasa did in this sense, but I would like to create a solution that follows the spirit of the original implementation. In other words, the user does need to be able to drag images around to customize the ordering and while they are doing this they feedback in the graphical view itself about where exactly the image(s) will end up if dropped in the tile view at any given location. 

While we will explore more of this later, one of the primary motivations behind this 'custom sorting view' is that the user will need to sort images that have no existing meaningful sorting applied to them - they were images scanned from negatives or a flatbed scanner and they have no timestamps or filenames that are representative of the images in any way. For this reason the user will need to manually arrange the order of the tiles to properly represent the chronological order and then the user will use some timestamp technique that we will also later discuss to then 'psuedo save' the custom ordering in the sense that the images are now timestamped and can then be sorted normally using the by date ordering mechanism.




## Timezones

The documentation may loosely refer to TZDATA which I believe is python specific thing for timezone database. Since we not using python in this project it likely doesn't make sense to use a python library or object for the timezone purposes. Therefore, this loose reference to TZDATA is really just referring to the general idea of whatever component should actually be used in pimio. The docs might eventually replace the tzdata language to clear up some of the confusion.

Pimion should make special effort to ensure media in a pimio library is tagged comprehensively enough to guarantee proper media organization in the library. Obviously one of the crucial pieces of information in this regard is timezones - and not only the timezone itself but the revision of the timezone! The specific IANA revision! I would hope and expected that most modern media formats do support such a metadata tag but realistically I expect that most do not support this and we will need to shimmy it in somehow.


### 🏛️ Timezone Management - High-Level Architectural Concepts

The system operates on a dual-strategy paradigm, encapsulating data location, parsing mechanics, and lifetime management into a unified layer. It uses a single, robust runtime engine based on the open-source industry standard (Howard Hinnant's timezone design) to handle both execution tracks, ensuring absolute behavioural consistency.

### 🏛️ Timezone Management - System Architecture Overview

The enhanced design introduces an **OS Profiling Engine** that probes the underlying platform to fingerprint its active zone database version, and an **Attributed Storage Format** that couples every saved timezone boundary with its database version context.


### ⚙️ Timezone Management - Component Breakdown

#### 1. Configuration & Strategy Selection
At startup, the application queries its configuration store (e.g., an environment variable, a command-line flag, or a configuration file).
* **Default Mode (OS-Reliant):** The engine initializes using environment defaults, automatically binding its lookup operations to the local machine's system filesystem paths or registry entries.
* **Overridden Mode (Custom Artifact):** The application suppresses standard OS paths and registers a dedicated target directory managed on the user’s file system.

#### 2. The Artifact Ingestion Pipeline (Post-Build Update)
When a user chooses to bypass the OS, they supply an external artifact post-compilation. The runtime manages this via the following steps:
* **The Target Artifact:** The application expects raw, textual geographic zone source definitions released by IANA (such as `northamerica`, `europe`, `backward`, `etcetera`).
* **Ingestion Method:** The user drops a compressed archive (`.tar.gz`) or points the application to an unzipped directory containing these raw files. If the application has network access, it can optionally contact IANA mirrors directly to fetch this payload.
* **Extraction & Structure Validation:** An abstraction layer ensures the folder contains vital structural files like the `backward` file (essential for legacy aliases) and core regional rulesets before processing.

#### 3. Dynamic Parser & Runtime Hot-Swapping
The core engine features a text-file compiler that executes entirely in memory after the application is built.
* **Decoupled Relocation:** When switched to Custom Mode, the subsystem explicitly redirects its search pointers to the custom extraction directory.
* **In-Memory Thread-Safe Swapping:** The compilation engine sweeps the textual files, builds an internal network of rule structures, offsets, and transition boundaries, and triggers a data-swap operation.
* **Instant Propagation:** Any subsequent timezone lookup anywhere else in the application immediately resolves against the newly constructed ruleset without restarting the application or dropping active network connections.

### 🔄 Timezone Management - Concrete Runtime Lifecycles

#### The Application Startup Sequence
1. The program starts and loads configuration preferences.
2. If **Strategy A (OS)** is set, the timezone runtime queries standard platform paths. If found, it populates the active lookup database.
3. If **Strategy B (Custom)** is set, the runtime overrides default parameters, verifies the existence of the custom source files, and processes the raw text assets directly into memory.
4. The global system locks the validated database state, signaling to all application modules that date-time conversions are safe to execute.

#### The On-the-Fly Update Sequence
1. While the system is actively running, a user triggers an "Update Time Zone Database" instruction and references a newly downloaded `tzdata` tarball.
2. A separate worker thread handles extraction to safeguard performance.
3. The validation subsystem checks the integrity of the raw text schemas.
4. The engine invokes an explicit database reload command, re-parsing the new file parameters.
5. The global application state pointer atomic-swaps to point to the freshly updated timezone data structures. Legacy queries finish executing under old rules, while all subsequent operations immediately bind to the new layout.


### 🎯 Timezone Management - Key Engineering Benefits of this Architecture

* **Identical Functional Types:** Because the underlying codebase leverages a single architecture to interpret both OS databases and raw IANA archives, application logic remains standard. Developers do not need to write split logic for Windows vs. Linux vs. Custom.
* **Zero System Dependencies in Isolation:** When running in Custom Mode, the application can survive on minimalist, air-gapped embedded platforms that lack a built-in operating system timezone system entirely.
* **Complete Upstream Transparency:** Users do not need to wait for a developer to recompile the application or issue a software patch when a global boundary shifts. They possess total autonomy to source raw data artifacts straight from the authoritative standard provider (IANA) and deploy them to the running program instantly.



### 🏛️ Timezone Version Metdata - System Architecture Overview

The enhanced design introduces an **OS Profiling Engine** that probes the underlying platform to fingerprint its active zone database version, and an **Attributed Storage Format** that couples every saved timezone boundary with its database version context.


### ⚙️ Timezone Version Metdata - Component Breakdown

#### 1. The OS Profiling & Version Extraction Engine
Because operating systems do not provide a unified endpoint, this module contains cross-platform, non-blocking probes executed during the fallback initialization phase:
* **POSIX / Linux Probe:** Executes lightweight file-checks or environment queries. It checks if package manager records are readable or scans `/usr/share/zoneinfo/` for known distro version files.
* **macOS Probe:** Explicitly reads the localized text stream from `/usr/share/zoneinfo/+VERSION`.
* **Windows Inference Engine (The Guessing Layer):** Because Windows maps things to its own format, if it cannot find an explicit version string, the application performs an in-memory test. It samples some number of historical political change points (e.g., “Did the Cairo offset change in May 2023 on this machine?”). Based on whether the OS applies the rule or not, the engine narrows down the match and tags it (e.g., `"Inferred-IANA-2023c"`). Ideally in development and testing we are able to confirm that on any given instance of windows 10/11 the timezone version can be successfully guessed.
* **Fallback Labeling:** If profiling completely fails, it tags the data with timezone version `"unknown"`.

#### 2. Self-Describing Data Schema (The "Label")
Your data storage layer is extended so that timestamps are never saved in isolation. Every temporal record contains an immutable **Metadata Context block**:
* **Timestamp:** The localized clock face value.
* **Zone Identifier:** The string name (e.g., `America/New_York`).
* **Database Provenance String:** The version discovered by the OS Profiling Engine at the exact moment the data was captured or last modified (e.g., `IANA-2022g`).

#### 3. The Reconciliation & Data Repair Module
When the user flags the application to switch from Strategy A (OS) to Strategy B (Custom Target: `2026b`), the application boots the custom database via Howard Hinnant's library. Instead of blindly applying the new database to old data, it triggers a **Time Zone Drift Analysis**:
* **Scan Phase:** The application scans the database index for records matching older provenance strings (e.g., `IANA-2022g`).
* **Simulation Phase:** For each unique time zone found in those old records, the engine calculates the underlying UTC epoch using both the old labeled version and the fresh `2026b` custom database.
* **Diff Generation:** If the UTC epochs match, the data is safe. If they drift (e.g., a 60-minute discrepancy due to a canceled DST law), the record is marked as `"Context-Drifted"`.



### 🔄 Timezone Version Metdata - Example User Repair Interaction Lifecycle

#### Step 1: Ingestion & Comparison View
When the user points the application to a downloaded IANA artifact, the UI displays a comparative report:
> **Active Environment Shift Detected:**
> * Current System Baseline: `IANA-2022g` (via Host OS Profiler)
> * Proposed Target Version: `IANA-2026b` (via Provided Custom Tarball)
> * Status: *Your OS database is 4 years out of date. 1,240 existing records are affected by historical rule variations.*

#### Step 2: Granular Resolution Wizard
The module presents the drifted rows to the user with two distinct programmatic options for rectification:
* **Option A (Preserve Wall-Clock Intent):** *"Keep the local time showing exactly 14:00:00, but recalculate the underlying UTC epoch to match the modern global laws specified in 2026b."*
* **Option B (Preserve Real-Moment UTC Intent):** *"The underlying physical moment was logged correctly despite the old OS label. Keep the absolute UTC timestamp intact, but change the local clock face string to reflect the corrected offset rules."*

#### Step 3: Metadata Sealing
Once the user selects a resolution path, the application processes the data blocks, updates the calculations, and swaps the Database Provenance metadata tag from `IANA-2022g` to `IANA-2026b`. The dataset is now completely healed, aligned, and marked with a clean audit trail.

