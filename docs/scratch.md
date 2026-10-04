# Pimio: addendum


## MCP Server

Pimio should have an MCP server with functions such as those below. This idea and this list needs more planning and development before being implemented:
- list_libraries()
- list_media(library, folder)
- get_media_metadata(id)
- get_version_history(id)
- restore_version(id, version)
- search_media


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




## Embracing Picasa Patterns

The implementation of the picasa ini file support should be implemented in two parts. First, a simple set of operations like rotate and resize should be supported. Later when other features are added to Pimio in a later implementation phase such as color corrections etc - then pimio would add corresponding support for any of those operations as the picasa.ini file defines - and in the short term it doesn’t mind that they show up in an existing ini for example it just wouldn’t be able to do anything with them.

https://github.com/dtmland/pimio/blob/main/docs/plan/picasa.md#64-picasa-non-destructive-editing
https://github.com/dtmland/pimio/blob/main/docs/plan/picasa.md#65-picasa-ini-file-format

### The Pimio Replay & Ingestion Pipeline

> [!WARNING]
> TODO: This section 'The Pimio Replay & Ingestion Pipeline' and its 'phases' does yet include the fact that this "replay & ingestion" process only applies specifically to importing media into a Pimio library. It is not as relevant for 'Browser' mode. The section wording and explanation needs to be updated to account for this.

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

### Groups of Interest (GOI)

Need to develop a concept of and I quote “group of interest” - in other words, a set of media the is identified using some unique pattern that they have in common or that they share in some form or another that could cue Pimio into knowing how they might be related in some way, if that way is not yet determined.

For example, if you have a set of images that were created from a flatbed scanner, using Old traditional print photos, the group of files that you end up with would have time stamps, likely from the day they and time that they were scanned and not the day and time which the photos themselves were, the prints were originally captured nevertheless, if we inspect the timestamps of the photos that are created, for example let’s say we have a folder of 100 images or photos that fall under this category 15 of the photos have timestamps that are much closer, and maybe an indication that that set was scanned in a single session by the person scanning them, which could be a clue to indicate the photos are related chronologically in some fashion granted this in no way guarantees that, but it could be an indication of that so if we look at the other 75 or I guess 85 photos in the set and we look for similar patterns or maybe 50 of them were scanned an hour later on the scanner then those might be their own group of interest because of this pattern that was used to recognize that set of 50 and the original set of 15 each would be its own unique group of interest

Another example again, referring to photos that may have been originally film, print photos, but scanned with a flathead scanner, and all the photos in this set, have the watermark date in the lower right hand corner of the photo that ideally does indicate when the photo was actually taken as long as the person using the camera had set the clock correctly on the camera, of course nevertheless, let’s just assume that in this set of scanned photos that they all have a date the dates are close to each other. Let’s say some of them appear to be in a single day and another set appears to be in another day so you would have two separate groups of interest. It isn’t clear what time during the day the photos were taken just the date is present from the watermark, so each set would be identified as a group of interest to allow the users of the Pio application to more easily identify and chronologically sort any given set of photos.

Photographic artifacts or specific metadata signatures that can determine whether: The image is “analog” (scanned from negative or print photograph - so no meaningful timestamps) or “digital” (older digital point-and-shoot camera with erroneous timestamps)The image is a “digital” capture of an “analog” photo (using a cell phone to snap a shot of a print photograph)

## GUI Layout and Function

### Custom Sorting

While the view already supports support viewing by name, date, type etc - there will also be a custom sorting mode where the sorting is user defined - user defined in the sense that the user would be able to grab a photo and drag it around in the tile array to change where it's place in the order of tiles (or photos) is. This feaure was available in picasa and while it did not refer to it like we are here as 'custom sorting mode' the idea was the same nevertheless. The user should even be able to select a group of photos and drag the entire group as a unit to change the place of ordering where the engire group fits. Picasa had this nice animation that would not only show the thumbnail tile (a translucent version of the tile actually!) that the user was dragging around but also the tiles near the area where the cursor was moving around the tile view would cause adjacent tiles to slightly shift away as if they were 'making room' for the new tile to fit in that location sort of anticpating if the user might drop the photo in the location. Not sure where picasa saved this custom ordering but again it never referred to it that it way maintined this user customized state if after picasa restarts. In picasa's case even after customizing the order to the tiles in this fashion the 'View->Folder View->Sory By' select did not change it would just stay at whatever the user had last selected. For pimio I am thinking that the user should explicitly enter a 'custom sort mode' using that the dropdown or whatever gui is used to control the sort type mode. At that point the user could then just click and drag a tile or a select a group of tiles and then grab and drag those around.

It might be that in pimio with QT we are not able to achieve quite the exact same behavior as picasa did in this sense, but I would like to create a solution that follows the spirit of the original implementation. In other words, the user does need to be able to drag images around to customize the ordering and while they are doing this they need feedback in the graphical view itself about where exactly the image(s) will end up if dropped in the tile view at any given location. 

While we will explore more of this later, one of the primary motivations behind this 'custom sorting view' is that the user will need to sort images that have no existing meaningful sorting applied to them - they were images scanned from negatives or a flatbed scanner and they have no timestamps or filenames that are representative of the images in any way. For this reason the user will need to manually arrange the order of the tiles to properly represent the chronological order and then the user will use some timestamp technique that we will also later discuss to then 'psuedo save' the custom ordering in the sense that the images are now timestamped and can then be sorted normally using the by date ordering mechanism.



## Designing for future Pimio Server

While the desktop installations of pimio should have full functionality on their own - it seems that perhaps a pimio server can supplement and add capability that is not possible with a single desktop instance. While the pimio server is out of scope for v1, the v1 desktop pimio should design for the future to accommodate features that will come in the future pimio server.

### Lore Server

One of the first ideas that came to mind was for pimio to run a lore server - and while the client already will have the ability to talk to a lore server to allow multi-user collaboration - it seems that in addition to that function a pimio server could go above and beyond. The pimio server could not only run a lore server, but also provide additional collaboration interfaces that each pimio client takes advantage of on top of the standard simple collaboration provided by a lone lore server.

### Timezone Finder Database

The pimio server can provide lookups for timezones using lat/long for each client - in this fashion each client does not need to download its own offline copy of the timezone lookup database.

### Large Tile Server Databases for high reslution offline browsing

While the desktop installation allows several online options to the user for map view including an offline option that can download a moderately sized open source or public domain tile set - the pimio server offers the ability to download a large sized open source or public domain tile set. In fact, in such an offline scenario where a pimio server is run each of the clients would then have the option to stream their tiles/data from the pimio server as they browse. In this fashion each clients also avoids the need to download/manage their own copy of the offline moderate size tile set.



