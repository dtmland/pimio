# Picasa Technical Architecture

## 1. Rendering Engine: OpenGL, Not DirectX

While Picasa was primarily a Windows app, its core user interface and real-time photo effects did not use Microsoft's proprietary DirectX or Direct3D framework. Picasa used OpenGL under the hood for all of its UI rendering.

- **Canvas Architecture:** When you browsed your timeline or zoomed into an image, Picasa did not draw using the slow Windows GDI (Graphics Device Interface). It treated the photo grid as a 3D canvas, with each photo tile mapped onto an OpenGL quad.
- **Why OpenGL Mattered:** Because Google opted for OpenGL 1.x/2.x instead of DirectX, the graphics pipeline was already compatible with macOS and Linux drivers. The GPU instructions were essentially portable across platforms.

### 1.1. Linux / Wine Acceleration

Because Picasa used OpenGL, the Linux Wine architecture avoided the performance penalties of translating DirectX to OpenGL (which, in the mid-2000s, was highly experimental and slow).

- **Direct Pass-Through via WGL:** In Windows, OpenGL talks to the operating system using WGL (Windows-to-OpenGL). When Picasa ran on Linux, the Wine layer translated WGL calls to GLX (or later to native Linux OpenGL), passing the graphics commands straight through to the host GPU.
- **Native Speed:** Because it was a near 1:1 translation of OpenGL instructions, Picasa on Linux had hardware-accelerated pan, zoom, and crossfades that ran at almost native speed, bypassing heavy CPU emulation.

### 1.2. Native macOS Port

The macOS port did not use Wine. While Google initially tried a Wine wrapper, they discarded it because OS X users expected native behaviors such as native menu bars, drag-and-drop, and standard macOS window chrome.

- **Core Logic Reuse:** Google kept the core business logic — the C++ db3 database engine, the background file watcher, and the photo processing code — largely intact.
- **Native UI Layer:** They stripped away the Windows-specific Win32 and MFC UI code and replaced it with a native Cocoa / Carbon UI layer.
- **Shared Rendering:** The Mac UI hooked into the same cross-platform OpenGL rendering pipeline used on Windows, ensuring identical fluid performance.

---

## 2. Database Engine (db3)

To replicate the speed and behavior of the Picasa Database Engine (internally known as the db3 sub-system), note that Google did not use a standard relational database like SQLite. The architectural components, file layouts, and synchronization loops are described below.

### 2.1. Columnar Storage Layout (.pmp Files)

Unlike traditional databases that store entire rows together, Picasa used a custom format called Picasa Media Property (.pmp).

- **One File Per Column:** Every "table" in Picasa was actually a collection of loosely coupled files sharing a common prefix. For example, instead of a single `images` table file, Picasa had files such as:
  - `imagedata_name.pmp` — filenames
  - `imagedata_path.pmp` — paths
  - `imagedata_date.pmp` — timestamps
  - `imagedata_size.pmp` — integers
- **Implementation Detail:** The data in these files was stored as a raw, contiguous array of binary values or fixed-width records. To get a complete "row" for an image, Picasa jumped to index `N` across all parallel `.pmp` files.

### 2.2. Dual-Layer State (Local .ini vs. Central Index)

Picasa operated on a "shared truth" model. The central database was merely an optimized cache of what existed in the file system.

- **The Sidecar (.picasa.ini):** Every folder contained a hidden plain-text `.picasa.ini` file. When a user cropped a photo, added a tag, or created a face-bound box, Picasa wrote the change there first.
- **The Central DB:** The central `.pmp` files read those `.ini` files and compiled them into optimized binary indexes. If the central database was wiped, Picasa could reconstruct the entire library — including edits and face tags — by re-reading the sidecars.
- **Design Implication:** Do not treat a central database as the ultimate source of truth. Write mutations to local text sidecars first, then asynchronously index those changes into binary caches.

### 2.3. Decoupled Thumbnail Blobs (.db Containers)

Picasa separated metadata from visual assets, keeping lightweight pre-rendered thumbnails in large monolithic files such as `thumbs.db` and `previews.db`.

- **Mechanism:** The `.pmp` metadata index mapped an image id to a byte offset inside `thumbs.db`. When rendering the UI grid, Picasa used memory-mapped files (`mmap`) to read the exact JPEG thumbnail by offset.
- **Implementation Implication:** Avoid creating individual thumbnail files on disk. Implement a single large binary blob container for thumbnails and keep an index of `(offset, length)` pairs in your image metadata.

### 2.4. Background Folder Watcher

Picasa's instantaneous updates relied on a continuous file-system monitoring loop.

- **Mechanism:** On Windows it heavily utilized the `ReadDirectoryChangesW` API; on Linux it used `inotify`.
- **Logic:** If a file was added, modified, or deleted, Picasa did not rescan the whole drive. It target-scanned that directory, updated the local `.picasa.ini`, and updated the matching `.pmp` index entries.

---

## 3. Image Pipeline

### 3.1. Image Pyramids / Mipmapping

Picasa allowed users to fluidly zoom into large images on computers with limited RAM by using image pyramids.

- **Mechanism:** When Picasa scanned a photo, it generated and cached the image at multiple distinct resolutions (e.g., 100px, 400px, 1024px).
- **UI Behavior:** When scrolling the grid, Picasa loaded the smallest thumbnails. When a user opened a photo, it displayed an intermediate preview instantly while a background thread prepared higher-resolution tiles.

### 3.2. Raw Image Decoding

Handling RAW files from hundreds of cameras (NEF, CR2, ARW) usually requires heavy licensing. Picasa used a highly optimized wrapper around open-source raw decoders.

- **Speed Optimization:** Instead of fully decoding a massive 14-bit RAW file just to show a grid thumbnail, Picasa's decoder looked for the embedded JPEG preview inside the RAW container and used that for the browser and initial viewer.
- **Progressive Demosaic:** While the user viewed the embedded preview, a low-priority background thread ran the heavy linear-to-RGB color matrix interpolation on the actual raw pixels. Once complete, the app swapped in the fully developed image.

### 3.3. Lossless JPEG Rotation

Unlike standard editors that decode, rotate, and re-compress (losing quality), Picasa used byte-stream manipulation for basic rotations.

- It rewrote the JPEG's EXIF orientation tag and, where possible, performed lossless block rearrangement rather than re-encoding the DCT coefficients.

---

## 4. Video Pipeline

Unlike its custom image engine, Picasa's video handling relied heavily on the host operating system's native multimedia frameworks.

### 4.1. DirectShow and QuickTime Pipeline Hooking

Picasa did not ship with video codecs. Instead it hooked into the native media subsystem of the host OS.

- **Windows (DirectShow):** Picasa built a DirectShow graph. For `.avi`, `.mpg`, `.wmv`, and similar files, it asked Windows to assemble the graph from registered DirectShow filters.
- **macOS (QuickTime API):** For Apple formats like `.mov` or early `.mp4` files, Picasa used the QuickTime API.
- **The Codec Trap:** Because Picasa relied on external system frameworks, it often broke whenever a user imported a camera video format unless they manually installed a third-party codec pack.

### 4.2. Video-to-Texture Interception (OpenGL Bridge)

Picasa played videos inside the same OpenGL canvas used for photos.

- **Mechanism:** Picasa used DirectShow's Video Mixing Renderer (VMR-7 or VMR-9) or a custom sample grabber callback filter.
- **The Trick:** Instead of letting DirectShow draw directly to the screen, Picasa intercepted the decoded video frames mid-flight and copied them into an OpenGL texture.
- **Result:** Because the video frame became a standard OpenGL texture, Picasa could rotate, zoom, or slide the video inside the timeline while it was actively playing.

### 4.3. Asynchronous Thumbnail Generation

To prevent a folder of video clips from freezing the UI:

- A background thread used DirectShow's `IMediaDet` interface to open the file stream without fully loading it.
- It skipped past initial black frames, grabbed a single RGB frame near the 1-second mark or a keyframe, resized it, and wrote it into the central thumbnail database.
- A small overlay icon distinguished video thumbnails from photo thumbnails in the grid.

### 4.4. Non-Destructive Video Trimming

Just like its photo edits, Picasa's basic trimming was non-destructive.

- Trimming parameters were written to the folder's `.picasa.ini` file.
- During playback, Picasa fed the start/end timestamps to DirectShow / QuickTime, instructing them to play only the requested range without re-encoding the original file.

---

## 5. Advanced Features

### 5.1. Local Facial Recognition (Neven Vision)

Long before on-device AI became common, Picasa featured fast local facial recognition.

- **Acquisition:** In 2006 Google acquired Neven Vision and integrated its technology into the local Picasa C++ desktop codebase.
- **On-Device Processing:** Picasa did not send photos to Google servers to find faces. The local engine analyzed photos, extracted facial geometry vectors (distances between eyes, nose shape, etc.), and grouped faces locally.

### 5.2. Picasa Web Albums Sync

Picasa was one of the earliest desktop apps to bridge local storage with the cloud via Picasa Web Albums (the precursor to Google Photos).

- **Non-Blocking Network I/O:** The upload engine was decoupled from the main UI thread. Users could select hundreds of photos, hit Upload, and keep editing while the queue processed in the background.
- **Two-Way Metadata Sync:** If a caption or geotag changed on the web, Picasa's background sync engine downloaded the diff, updated the central `.pmp` database, and rewrote the local `.picasa.ini` sidecar.

### 5.3. Real-Time Preview Pipeline

Applying filters felt instantaneous because of a non-destructive preview pipeline.

- **Instruction Stacking:** Applying a filter did not modify bytes on disk; it appended an instruction string such as `enhance=1;crop=rect(...)` to a list.
- **On-the-Fly Shaders:** The OpenGL engine took the base preview image and ran those text commands through real-time pixel shaders, so the user saw the final result as they adjusted sliders.

### 5.4. Progressive "Fuzzy" Search

Picasa's search-as-you-type bar filtered large libraries in real time.

- **Interleaved Keys:** As the user typed character by character (e.g., `M` → `Ma` → `Matt`), Picasa did not query disk. It kept a lightweight, highly compressed in-memory trie or inverted index of text tokens.
- **Pointer Jump:** The index pointed directly to array indices inside the columnar `.pmp` database, so the OpenGL canvas could drop non-matching tiles from the viewport without reloading metadata.

---

## 6. System Architecture

### 6.1. Dual-Core CPU Load Balancing

Picasa rose to prominence as multi-core processors (e.g., Intel Core 2 Duo) reached consumers. Google built a custom asynchronous thread pool.

- **UI Priority:** The UI thread was prioritized above all else. During scrolling, the majority of CPU resources went to the OpenGL texture-binding pipeline.
- **Stealing Worker Threads:** When user input paused, background workers woke up to split heavy tasks across cores — facial geometry computation, thumbnail generation, raw development, and upload queueing.

### 6.2. Radical Data Resilience

Unlike most Windows apps of the 2000s, Picasa avoided relying on the Windows Registry for application state.

- **Self-Healing Index:** The central database was transient. Copying photo folders to a new computer preserved custom albums, starred photos, face-tags, and non-destructive edits.
- **Reconstruction:** Because state lived in `.picasa.ini` files, a fresh Picasa install could reconstruct a large, old library in minutes just by scanning folders.


### 6.3. Picasa non-destructive editing

The interaction between **Picasa**, **`.picasa.ini` files**, and the hidden **`.picasaoriginals`** directory represents one of the earliest mainstream implementations of non-destructive editing. 

The exact lifecycle of a file during modification and saving follows a precise operational flow:

### Phase 1: Making Modifications (The Real-Time Overlay)
When you alter an image (e.g., crop, apply a filter, adjust contrast), Picasa **never alters your original file** on the disk. 

1. **State:** Your original file (`photo.jpg`) remains entirely untouched in its parent folder.
2. **Writing the INI:** The moment you make an edit, Picasa writes the exact parameters of that modification into a hidden text file named **`.picasa.ini`** (or `Picasa.ini` in older versions) located within that same folder. For example, it appends a block mapping to `photo.jpg` tracking instructions like `crop=rect(6867,b9a,908c,ed64)` or `filters=1`.
3. **Picasa View:** When you view the image inside Picasa, the app reads the original `photo.jpg` and applies the instruction set from `.picasa.ini` on-the-fly. 
4. **External View:** If you look at `photo.jpg` using a native explorer window or another app (like Facebook or Photoshop), you will only see the unedited original, because the file itself has not changed.

---

### Phase 2: Clicking the "Save" Button (The Physical Swap)
When you click **"Save to Disk"** on a folder or select **File > Save**, Picasa hard-commits the changes to disk so that external programs can see your edits. It performs a seamless file substitution:

1. **Creating the Hidden Directory:** Picasa creates a hidden subfolder called **`.picasaoriginals`** directly inside the parent folder if it doesn't already exist. 
2. **Moving the Original:** The untouched original file (`photo.jpg`) is moved out of the main directory and into the `.picasaoriginals` subfolder. 
3. **Baking the Edits:** Picasa generates a brand-new JPEG matching your applied modifications. It writes this newly edited image file directly into the main parent directory, naming it exactly what the original was (`photo.jpg`).
4. **Updating the INI:** The `.picasa.ini` file is updated to flag that the file has been saved. This instructs Picasa's internal database to track that an "Undo" state exists inside `.picasaoriginals`.

**The result outside Picasa:** Windows Explorer and other programs now see the modified file because it has overwritten the primary file slot. Your storage footprint for that photo doubles.

---

### Phase 3: The "Undo Save" Request (The Reverse Flow)
Because Picasa meticulously manages this ecosystem, you can reverse a saved state at any point by clicking **Undo Save** or **File > Revert**. 

1. **Reading the State:** Picasa checks `.picasa.ini` to verify that an original file is cached in the `.picasaoriginals` directory.
2. **The Restoration:** Picasa deletes the edited `photo.jpg` sitting in the parent folder.
3. **The Swap-Back:** It moves the true original `photo.jpg` out of the hidden `.picasaoriginals` folder and places it back into the main parent folder. 
4. **Reverting the INI:** The `.picasa.ini` file strips the "saved" status but safely **retains your edit parameters**. This restores you exactly to Phase 1: external programs see the unedited original again, but Picasa still displays your modifications on-the-fly.

### 6.4. Supported file types

Picasa supports a wide variety of standard image, video, and camera RAW file formats for viewing, importing, and organizing. 

### Supported Image Formats
* **.jpg / .jpeg**: Standard JPEG image files (always scanned and displayed by default).
* **.bmp**: Windows bitmap files.
* **.gif**: Graphics Interchange Format (can be enabled in options).
* **.png**: Portable Network Graphics (can be enabled in options).
* **.tif / .tiff**: Tagged Image File Format.
* **.psd**: Adobe Photoshop documents.

### Supported RAW Image Formats
Picasa performs an on-the-fly conversion to display RAW files from many major camera manufacturers:
* **Adobe**: `.dng`
* **Canon**: `.crw`, `.cr2`
* **Casio**: `.raw`
* **Fuji**: `.raf`
* **Hasselblad**: `.3fr`
* **Kodak**: `.dcr`, `.kdc`
* **Leica**: `.dng`, `.raw`
* **Minolta**: `.mrw`
* **Nikon**: `.nef`, `.nrw`
* **Olympus**: `.orf`
* **Panasonic**: `.raw`, `.rw2`
* **Pentax**: `.pef`
* **Ricoh**: `.dng`
* **Samsung**: `.dng`
* **Sigma**: `.x3f`
* **Sony**: `.arw`, `.srf`, `.sr2`

### Supported Movie and Video Formats
* **.avi**: Audio Video Interleave
* **.mpg / .mpeg**: MPEG video files
* **.wmv**: Windows Media Video
* **.asf**: Advanced Systems Format
* **.mov**: Apple QuickTime movie format
* **.mp4**: MPEG-4 video format (supported in later Picasa 3 builds)




### 6.5. Picasa Non-Destructive Editing

https://gist.githubusercontent.com/fbuchinger/1073823/raw/9986cc61ae67afeca2f4a2f984d7b5d4a818d4f0/.picasa.ini

The `.picasa.ini` file is a hidden, configuration file generated by **Google Picasa** in each image directory. It manages non-destructive editing history, face-tagging zones, album relationships, and custom folder metadata.

---

## Data Type Encoding Guide

Picasa utilizes several custom serialization formats within its configuration keys:

### 1. `rect64()` (Relative Bounding Boxes)
Used heavily for image cropping rectangles and detected face boundaries. 
* **Structure**: Encases a 64-bit hexadecimal string representing four 16-bit integers: `rect64(LeftTopRightBottom)` (e.g., `rect64(3f845bcb59418507)`).
* **Decoding Mechanism**:
  1. Break the string into 4 equal 4-character substrings (`3f84`, `5bcb`, `5941`, `8507`).
  2. Convert each substring from hexadecimal to a standard integer.
  3. Divide by the maximum 16-bit integer value (`65536`) to obtain a fractional float between `0` and `1`.
  4. **Formula**: 
     $$\text{Absolute X} = \text{Fractional Coordinate} \times \text{Image Width}$$
     $$\text{Absolute Y} = \text{Fractional Coordinate} \times \text{Image Height}$$

### 2. Fractional Floats & Floats
* **Fractional Floats**: Specified with 6 decimal places of precision bounded between `0` and `1` (e.g., `0.306743`). Commonly used for relative UI elements, mouse points, or opacity configurations.
* **Standard Floats**: Typical decimal representations used to adjust fine-tuning sliders like highlights or shadows.

### 3. Hexadecimal Hashes
* **Hashes**: Represent internal signatures (e.g., `originhash=033f1132c87469f2bb7c5f4e41c18e6b`) evaluating file states or detecting changes made externally outside of Picasa.

---

##  Section Implementations

### 1. The Global Folder Section `[Picasa]`
This contains metadata treating the execution context of the entire local directory as a unified album or collection item.

```ini
[Picasa]
name=Foo Bar birthday      ; Visual display name assigned to the folder/album
category=Folders on Disk   ; Standard category descriptor (e.g., Local vs Cloud)
```

####  Web Sync Metadata
If synced or generated from **Picasa Web Albums** / Google Photos, the following specific patterns appear:
* `P2category=Downloaded Albums~otheruserid` — Distinguishes downloaded folders from source creation tracks.
* `<username>_lh=<album_id>` (e.g., `joedoe_lh=5620038667642797505`) — Tracks explicit web ownership relationships and structural backend catalog numbers.

---

### 2. The Photo Entries Section `[Filename.ext]`
Every asset in the directory requiring modifications, metadata enhancements, or recognition maps contains an explicit entry header named after its literal file name.

```ini
[2012-08-07 10.55.16.jpg]
caption=Summer Vacation 2026       ; Custom text captions applied natively
keywords=Key1,Key2                 ; Comma-delimited list of tagging tags
geotag=33.770556,-84.293055        ; Raw Latitude and Longitude floats
height=3456                        ; Preserved pixel dimensions
width=4608                         ; Preserved pixel dimensions
backuphash=48903                   ; State identifier tracking backup changes
originhash=033f1132c87469f2bb7c    ; Primary snapshot checksum validation 
moddate=8094e2826277cd01           ; Hexadecimal timestamp signature mapping updates
IIDLIST_name_lh=4da0bbdd9d705362   ; Internal list hierarchy unique mapping key
```

---

### 3. Face Tagging & Identification Protocols

Face identification configurations map directly inside individual photo entry rows using paired lists:

```ini
faces=rect64(18fe0cc96f9357f5),632e71e2ffd6c6d;rect64(97070ba0ef375859),632e71e2ffd6c6d
```

* **Format Syntax**: `faces=rect64(<BoundingBox>),<ContactID>;...`
* **Delimiters**: Comma (`,`) binds a face coordinates box to its identifier; a semicolon (`;`) stacks multiple individuals discovered on a single file resource.
* **`<ContactID>` References**: This maps back to a unique person contact dictionary entry stored globally within Picasa or in the specific folder context block.

---

### 4. Non-Destructive Editing & Filter Chains

Picasa saves every manipulation as serial instruction rules instead of directly breaking pixels on disk until a literal save operation executes:

#### Core State String Arrays
* **`filters=`**: A semicolon-separated chronological string stack representing every image filter and adjustment applied.
* **`redo=`**: Holds equivalent programmatic steps to rebuild transformations when undoing or stepping backwards through local session adjustments.

#### Example Filter Tokens & Parameters
* `autocolor=1` — Automated color channel balance adjustment.
* `autolight=1` — Exposure levels automatic contrast adjustment.
* `enhance=1` — Global sharpness/intensity saturation enhance.
* `crop64=1,10000000f1ddff49` — Crop filter applying an internal 64-bit coordinates mask.
* `finetune2=1,0.333333,0.176842,0.193684,00000000,0.000000` — Slider fine-tuning map detailing values such as highlights, shadows, color temperature, and tint settings.

