
## Timezones

> [!WARNING]
> The pimio documentation may loosely refer to TZDATA which I believe is python specific thing for timezone database. Since we not using python in this project it likely doesn't make sense to use a python library or object for the timezone purposes. Therefore, this loose reference to TZDATA is really just referring to the general idea of whatever component should actually be used in pimio. We should eventually replace the tzdata language to clear up some of the confusion.

Pimion should make special effort to ensure media in a pimio library is tagged comprehensively enough to guarantee proper media organization in the library. Obviously one of the crucial pieces of information in this regard is timezones - and not only the timezone itself but the revision of the timezone! The specific IANA revision? Should 'timezone offsets + IANA version' be stored in each image metadata? Or should the IANA version
only be stored at the library level?

Should lookups timezone using location be achieved using some kind of rest server that pimio runs under the hood? I ask this because it appears many of the options in the tzf project want to run in some kind of web server. I also ask this because I was thinking a cool future feature could be that pimio can be pointed to a future hypotheical pimio server in order to perform its lookups. The idea there is that each desktop instance that is pointing to the pimio server would not have to download/manage its own tz databases copy and only the pimio server would have to do that. The pimio server could have other functions this would just be one of them.

Heirarchy for timezone lookups
1) If online, google time zone API? Requires some kind of API key? Other online services, they just as painful api key wise?
2) Offline options?
	Can use this rust library in c++? https://github.com/ringsaturn/tzf-rs
	Use this instead? Not as good as tzf? https://github.com/BertoldVdb/ZoneDetect
	Is Howard Hinnant's library relevant if already using options from above?
	Download components with Add-Ons Manager? 
		Sounds like if we use tzf then we are never getting IANA ourselves directly? Only indirectly via tzf-dist?
			IANA database?
			tzf-dist? https://github.com/ringsaturn/tzf-dist

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

---

# The Provenance Paradox: Why Accurate Metadata Requires Pairing the IANA Version with Explicit Offsets

Storing timestamps and GPS coordinates is a massive leap forward for photo metadata, but it remains an incomplete solution. When a photo contains an explicit `OffsetTime` tag (e.g., `-05:00`), it records a hardcoded mathematical reality. However, without knowing **why** that offset was chosen, the metadata lacks context.

Pairing the specific **IANA Timezone Database Version** (e.g., `IANA-2024a`) directly with the `OffsetTime` tags provides critical security, context, and long-term durability for digital assets.

---

### The Fatal Flaw of Independent Metadata

If an application or camera stores local time, GPS location, and a static offset independently, it creates a ticking time bomb for data integrity. The value of locking the IANA database version alongside these fields addresses three critical engineering challenges:

#### 1. Preventing the "Host OS Correction" Loop

When an up-to-date operating system imports an old photo with a static offset and GPS coordinates, it checks the coordinates against its *current* timezone database.

* **The Conflict:** If a government retroactively altered a daylight saving rule for that historical date *after* the photo was taken, the modern OS database will calculate a different offset than what the camera originally stamped.
* **The Solution:** By explicitly labeling the photo with the database version used at the moment of capture (e.g., `Captured under: IANA-2015g`), the viewing software instantly understands the discrepancy. It can determine that the camera wasn't glitched—it was simply operating under a version of global time laws that has since been rewritten.

#### 2. Resolving Ambiguity in Political Border Shifts

Geopolitical borders change, and with them, the time zone boundaries mapped inside the IANA database.

* **The Conflict:** A photo taken in a disputed region might have explicit GPS coordinates that fall into Zone A under a 2014 database, but map to Zone B under a 2026 database.
* **The Solution:** Pairing the IANA version creates an unshakeable audit trail. It tells future software exactly which political epoch and geographic polygon map was active when the camera evaluated its location, removing all room for downstream software to misinterpret the asset's timeline.

#### 3. Preserving the Truth of Air-Gapped Configurations

As previously established, photos captured or edited on air-gapped or legacy machines often suffer from outdated OS databases.

* **The Conflict:** If an un-updated machine bakes an obsolete, incorrect offset into a photo's metadata, a modern system importing that photo will see a clash between the GPS coordinates and the `OffsetTime`. The modern software is forced to guess: *Is the GPS wrong, or is the offset wrong?*
* **The Solution:** If the metadata explicitly states `OffsetTime: -04:00` paired with `TZDatabaseProv: IANA-2012b`, the modern application instantly recognizes the root cause. It knows the offset was calculated by an obsolete system. The software can then safely perform a **data repair routine**, recalculating the correct offset using the GPS coordinates without corrupting the original metadata trail.

---

### Summary: The Ultimate Metadata Pair

| Metadata Strategy | What It Knows | What It Misses | Risk Over Time |
| --- | --- | --- | --- |
| **Timestamps + GPS Only** | Where and when the photo physically existed. | The actual clock face value seen by the photographer. | **High**: Software must guess the offset, often scrambling travel timelines. |
| **Timestamps + GPS + Offset** | The exact local time and its mathematical relation to UTC. | **The Political Context**: *Why* that offset was applied or what rules governed it. | **Moderate**: Future database updates or zone merges can cause the calculation to drift. |
| **Timestamps + GPS + Offset + IANA Version** | The complete physical, mathematical, and geopolitical ground truth. | Nothing. The metadata is completely self-describing. | **Zero**: Immune to future OS update cycles, historical rule changes, or software drift. |

### The Photo Metadata Guessing Hierarchy

| Tier | Available Metadata | Software Action | Risk Level |
| --- | --- | --- | --- |
| **1. Primary** | Local Time + GPS UTC Time | Direct mathematical deduction of offset. | **Perfect Accuracy** |
| **2. Secondary** | Local Time + Lat/Long Coords | Looks up coordinates in a geographic timezone database. | **High** (Fails if host OS database is out of date) |
| **3. Tertiary** | Local Time Only (Batch context) | Steals the offset from neighboring smartphone photos in the same batch. | **Moderate** (Relies on adjacent devices being set correctly) |
| **4. Fallback** | Local Time Only (Isolated) | Forces the photo into the current timezone of the importing computer. | **Extreme** (Destroys timeline chronology for travel photos) |

### OS Baseline vs. Application IANA Override

| Feature / Scenario | Relying on Host OS Database | Using Custom App-Level IANA Database |
| --- | --- | --- |
| **System Freedom** | **Chained to OS Update Cycles**: Requires a system restart or administrator rights to change. | **Portable & Instant**: Swapped inside the application interface instantly without root access. |
| **Air-Gap Compliance** | **Poor**: System remains unpatched and inaccurate if disconnected from update networks. | **Excellent**: Easily updated via a single static file brought in via a secure transfer. |
| **Historical Accuracy** | **Variable**: Focuses primarily on modern rules; older OS versions drop historical edge cases. | **Perfect**: Can load specific historical database versions to reconstruct past political boundaries. |
| **Cross-OS Harmony** | **Low**: Windows and macOS interpret rules differently, causing team timeline conflicts. | **Absolute**: Guaranteed bit-for-bit chronological parity across every platform. |

### The Chronological Blindspots of Modern IANA Updates

| Forensics/Archival Use Case | Why the Latest Database Fails | The Critical Value of an Older IANA Database |
| --- | --- | --- |
| **1. Zone Merges & Deletions** | Modern updates regularly delete or merge historic zones to clean up the code, erasing unique pre-1970 regional rules. | Preserves defunct local zone boundaries to prevent historical timeline calculations from being flattened into a neighbor's rules. |
| **2. Explaining Past Software Bugs** | The latest database fixes historical errors, masking how a glitchy server calculated an impossible offset years ago. | Reconstructs exactly what an old operating system "thought" was true at the time, proving software bugs over malicious forgery. |
| **3. Stabilizing Retroactive Edits** | Historians constantly update pre-1970 rules, causing a modern database to retroactively shift the clock on long-finalized records. | Pins the database to a fixed version, guaranteeing that millions of previously cataloged historical photos don't unexplainably drift. |
