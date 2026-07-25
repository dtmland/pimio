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
| Qt Qml / Qt Quick | 6.8.3 | default | Dynamically linked shared library | LGPL-3.0 | https://download.qt.io | No | Yes |

Qt is used under the LGPL dynamic-linking path. The application must keep Qt
replaceable by the user, ship the required license text and notices, and avoid
static linking unless a commercial license is obtained.

## Build- and test-only dependencies (not redistributed)

| Component | Version | Purpose | License | Redistributed? |
| --- | --- | --- | --- | --- |
| CMake | >= 3.24 | Build system | BSD-3-Clause | No |
| Ninja | any recent | Build backend | Apache-2.0 | No |
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
[pimio-v1-tools-environment.md](../pimio-v1-tools-environment.md). They are
listed here only so their license questions are not discovered late. None of
them are in the build yet, and each requires its own decision spike before
integration.

| Candidate | Purpose | Known licensing concern |
| --- | --- | --- |
| LORE | Versioned ground-truth store | Availability, packaging, and license unknown. Blocked by the Increment 2 feasibility gate. |
| SQLite | Local projection and query cache | Public domain. Low risk. |
| libexiv2 | EXIF/IPTC/XMP read and write | GPL-2.0-or-later. Linking strategy must be resolved before distribution. |
| libraw | RAW decode | LGPL-2.1 / CDDL dual license. |
| libjpeg-turbo | Lossless JPEG transforms | Permissive. Low risk. |
| FFmpeg | Video decode, thumbnails, trim | License depends on configure flags. An LGPL build is required unless the product license changes. |
| x264 / x265 | H.264 / HEVC encode | GPL, plus HEVC patent considerations. |
| MapLibre GL (Qt plugin) | Map rendering | BSD. Tile-provider terms are separate. |
| OpenCV | Optional analysis | Apache-2.0. |
