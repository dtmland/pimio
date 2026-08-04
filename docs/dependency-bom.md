# Dependency Bill of Materials

Status: **stub established in Increment 0.** It currently records only the
dependencies actually used by the repository. Every new dependency must be
added here in the same pull request that integrates it, with exact version,
enabled features, runtime or process boundary, license, notices, source, and
redistribution status.

Legal review is a release gate. CI cannot certify it.

## Runtime dependencies (redistributed)

| Component | Version | Features enabled | Boundary | License | Source | Modified? | Redistributed? |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Qt Core | 6.8.3 | default | Dynamically linked shared library | LGPL-3.0 | https://download.qt.io | No | Yes |
| Qt Gui | 6.8.3 | default | Dynamically linked shared library | LGPL-3.0 | https://download.qt.io | No | Yes |
| Qt Sql | 6.8.3 | QSQLITE driver | Dynamically linked shared library, SQLite bundled by Qt | LGPL-3.0 | https://download.qt.io | No | Yes |
| Qt Qml / Qt Quick | 6.8.3 | default | Dynamically linked shared library | LGPL-3.0 | https://download.qt.io | No | Yes |
| Qt Image Formats | 6.8.3 | WebP plugin with bundled libwebp | Dynamically loaded Qt image plugin | LGPL-3.0 | https://download.qt.io | No | Yes |
| libavif | 1.4.2 | AVIF read with libaom | Statically linked through the AVIF Qt image plugin | BSD-2-Clause | https://github.com/AOMediaCodec/libavif/releases/tag/v1.4.2 | No | Yes |
| libaom | 3.14.1 | AV1 decoder only | Statically linked into libavif | BSD-2-Clause | https://aomedia.googlesource.com/aom/+/refs/tags/v3.14.1 | No | Yes |
| qt-avif-image-plugin | 0.10.3 | Qt 6 image I/O adapter, read capability enabled | Statically linked Qt image plugin | BSD-2-Clause | https://github.com/novomesk/qt-avif-image-plugin/releases/tag/v0.10.3 | No | Yes |
| LORE (`liblore`) | 0.8.5 | C API, offline and local-only | Dynamically loaded shared library, resolved at runtime through `QLibrary` | MIT | https://github.com/EpicGames/lore/releases/tag/v0.8.5 | No | Yes |

Qt is used under the LGPL dynamic-linking path. The application must keep Qt
replaceable by the user, ship the required license text and notices, and avoid
static linking unless a commercial license is obtained.

AVIF decoding uses the maintained libavif codec and qt-avif-image-plugin
adapter rather than a project-specific decoder. They and libaom are pinned and
fetched from their tagged upstream releases; all three use permissive
BSD-2-Clause terms.

Metadata is read by `pimio::metadata`, parsers written for this project, so the
read path adds no row here. The reasoning and what would reverse it are in
[decisions/0002-metadata-adapter.md](decisions/0002-metadata-adapter.md).

SQLite reaches pimio through Qt's bundled QSQLITE driver rather than as a
separate dependency, so it carries no obligation of its own. If pimio ever
links a system SQLite directly, this table gains a row for it.

LORE is redistributed, so packaging must ship both files that come with the
artifact: its MIT `LICENSE.txt` and its `THIRD-PARTY-NOTICES.txt`, which covers
the Rust crates statically linked into `liblore`. Neither file may be dropped
in favour of a summary. This obligation is part of the Increment 12 packaging
work; the artifact is already extracted with both files intact by
`cmake/PimioLore.cmake`.

The boundary is deliberately narrow. `lore.h` is private to `src/lore/`, the
library is resolved by name at runtime rather than linked at build time, and the
adapter degrades to an unavailable state when it cannot be loaded. A version
bump is therefore contained to `cmake/PimioLore.cmake` and `src/lore/src/`.

## Build- and test-only dependencies (not redistributed)

| Component | Version | Purpose | License | Redistributed? |
| --- | --- | --- | --- | --- |
| LORE CLI (`lore`) | 0.8.5 | Independent verification that pimio's repositories are readable by the reference implementation | MIT | No |
| CMake | >= 3.24 | Build system | BSD-3-Clause | No |
| Ninja | any recent | Build backend | Apache-2.0 | No |
| NASM | any recent | Assemble libaom's optimized AV1 routines | BSD-2-Clause | No |
| Qt Test | 6.8.3 | Unit and smoke tests | LGPL-3.0 | No |
| Xvfb | distribution version | Linux X11 test display | MIT | No |

## GitHub Actions dependencies (CI only)

| Action | Version | Purpose |
| --- | --- | --- |
| `actions/checkout` | v4 | Repository checkout |
| `actions/upload-artifact` | v4 | Publish test results |
| `jurplel/install-qt-action` | v4.3.1 | Install the pinned Qt release |
| `seanmiddleditch/gha-setup-ninja` | v6 | Install Ninja on Windows and macOS |
| `ilammy/msvc-dev-cmd` | v1 | MSVC developer environment on Windows |

## Anticipated dependencies (not yet integrated)

These are candidates recorded in
[pimio-v1-tools-environment.md](plan/pimio-v1-tools-environment.md). They are
listed here only so their license questions are not discovered late. None of
them are in the build yet, and each requires its own decision spike before
integration.

| Candidate | Purpose | Known licensing concern |
| --- | --- | --- |
| libexiv2 | EXIF/IPTC/XMP write, and read coverage beyond plain container headers | GPL-2.0-or-later. Linking strategy must be resolved before distribution. Increment 5 declined it for the read path; see [decisions/0002-metadata-adapter.md](decisions/0002-metadata-adapter.md). |
| libraw | RAW decode | LGPL-2.1 / CDDL dual license. |
| libjpeg-turbo | Lossless JPEG transforms | Permissive. Low risk. |
| FFmpeg | Video decode, thumbnails, trim | License depends on configure flags. An LGPL build is required unless the product license changes. |
| x264 / x265 | H.264 / HEVC encode | GPL, plus HEVC patent considerations. |
| MapLibre GL (Qt plugin) | Map rendering | BSD. Tile-provider terms are separate. |
| OpenCV | Optional analysis | Apache-2.0. |
