# pimio v1 — Tools, Environment & CI Strategy

## 1. My Sandbox Environment

I (GitHub Copilot agent) run in a standard **Ubuntu Linux x86_64** environment. The table below
shows what is and is not available to me directly.

| Capability | Available? | Notes |
|---|---|---|
| apt package manager | ✅ Yes | Full access |
| GCC 13+ / Clang 17+ / CMake / Ninja | ✅ Yes | Standard Linux build toolchain |
| Qt 6 (via apt) | ✅ Yes | `qt6-base-dev`, `qt6-declarative-dev`, etc. |
| Qt 6 (via aqtinstall) | ✅ Yes | Python tool — downloads any Qt version/module without the Qt GUI installer |
| SQLite | ✅ Yes | `libsqlite3-dev` |
| libexiv2 | ✅ Yes | `libexiv2-dev` |
| libraw | ✅ Yes | `libraw-dev` |
| FFmpeg libraries (LGPL build) | ✅ Yes | `libavcodec-dev`, `libavformat-dev`, etc. |
| OpenCV | ✅ Yes | `libopencv-dev` |
| Headless Qt/QML tests | ✅ Yes | `QT_QPA_PLATFORM=offscreen` |
| Qt Creator / Designer | ❌ No | GUI IDE — not usable headless |
| Windows compiler (MSVC / MinGW) | ❌ No | Cross-compilation possible but no real runtime test |
| macOS / Xcode / Metal | ❌ No | Completely unavailable |
| Real GPU / hardware acceleration | ⚠️ Limited | No GPU — software rendering only |
| Physical display / Wayland | ❌ No | No display server — offscreen only |
| MapLibre GL native rendering | ⚠️ Limited | Software render path only |
| Code signing (Windows / macOS) | ❌ No | Requires certificates and those operating systems |

---

## 2. Full Tool Inventory

### Core Build & Toolchain

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| CMake ≥ 3.24 | Build system | ✅ Free | ✅ Yes | Industry standard for Qt C++ |
| Ninja | Fast build backend | ✅ Free | ✅ Yes | Faster than make |
| GCC 13+ / Clang 17+ | C++ compiler (Linux) | ✅ Free | ✅ Yes | Both available via apt |
| MSVC (Visual Studio Build Tools) | C++ compiler (Windows) | ✅ Free* | ❌ No | *Build tools are free; full VS is paid |
| Xcode Command Line Tools | C++ compiler (macOS) | ✅ Free | ❌ No | Requires a free Apple Developer account |
| MinGW-w64 | Cross-compile to Windows | ✅ Free | ⚠️ Partial | Cross-compile only — no real runtime test |

### Qt 6

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| Qt 6 Open Source (LGPL) | Core framework, QML, widgets | ✅ Free | ✅ Yes | Must comply with LGPL (dynamic linking) |
| Qt Multimedia | Audio/video playback | ✅ Free | ✅ Yes | Uses platform backends or FFmpeg |
| Qt Location / Maps | Map view | ✅ Free | ✅ Yes | MapLibre backend recommended |
| Qt Quick / QML | UI layer | ✅ Free | ✅ Yes | |
| Qt File System Watcher | Folder watching | ✅ Free | ✅ Yes | |
| Qt SQL | SQLite integration | ✅ Free | ✅ Yes | |
| Qt Test | Unit + QML testing | ✅ Free | ✅ Yes | Headless with offscreen platform |
| aqtinstall | Downloads Qt without GUI installer | ✅ Free | ✅ Yes | Required for CI environments |
| Qt Commercial License | Static linking + commercial support | 💰 Paid | N/A | Not required if LGPL path is followed |

### Media & Metadata Libraries

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| libexiv2 | EXIF/IPTC/XMP read+write | ✅ Free (GPL/LGPL) | ✅ Yes | GPL — review linking strategy before distribution |
| libraw | RAW image decode | ✅ Free (LGPL/CDDL) | ✅ Yes | |
| libjpeg-turbo | JPEG decode / lossless transform | ✅ Free | ✅ Yes | Needed for lossless EXIF-preserving rotation |
| FFmpeg (LGPL build) | Video decode, thumbnails, trim | ✅ Free | ✅ Yes | Avoid GPL codecs to keep distribution simpler |
| x264 / x265 | H.264 / HEVC encode | ⚠️ Complex | ✅ Yes | Both GPL; HEVC also carries patent licensing considerations |
| OpenImageIO / stb_image | Supplemental image decode | ✅ Free | ✅ Yes | |

### Storage & Versioning

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| SQLite | Local cache / index | ✅ Free (Public Domain) | ✅ Yes | |
| LORE | Versioned ground-truth store | ✅ Free (if OSS) | ⚠️ Unknown | Availability and packaging is a wildcard for v1 |

### AI & Computer Vision

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| OpenCV | Face detection, landmark detection | ✅ Free (Apache 2) | ✅ Yes | Good offline baseline |
| dlib | Face detection / recognition | ✅ Free | ✅ Yes | Better accuracy than OpenCV for faces |
| ONNX Runtime | Run ML models offline | ✅ Free | ✅ Yes | Can run quantized face/landmark models |
| InsightFace / MediaPipe models | Pre-trained face models | ✅ Free (mostly) | ✅ Yes | License varies per model — verify before shipping |
| Apple Vision / Core ML | macOS/iOS face + landmark | ✅ Free | ❌ No | macOS only |
| Windows ML | Windows face APIs | ✅ Free | ❌ No | Windows only |

### Maps

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| MapLibre GL (via Qt plugin) | Map rendering | ✅ Free (BSD) | ✅ Yes (software) | Best Qt-native option |
| OpenStreetMap tile data | Offline/online map tiles | ✅ Free | ✅ Yes | Tile hosting via OSM or self-hosted |
| Mapbox | Commercial tile hosting | 💰 Paid (free tier) | N/A | Not needed if using OSM |
| Stadia Maps / MapTiler | Tile hosting alternatives | 💰 Paid (free tier) | N/A | Optional |

### Testing

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| Qt Test (QTest) | Unit / integration testing | ✅ Free | ✅ Yes | |
| Catch2 / GTest | Additional C++ unit testing | ✅ Free | ✅ Yes | |
| CTest | Test runner (CMake) | ✅ Free | ✅ Yes | |
| AddressSanitizer / UBSAN | Memory and UB checks | ✅ Free | ✅ Yes | GCC/Clang built-in |
| Valgrind | Memory profiling | ✅ Free | ✅ Yes | Linux only |
| lcov / gcovr | Code coverage | ✅ Free | ✅ Yes | |
| GUI manual testing | Real app testing | — | ❌ No | Requires a real display |

### Packaging & Distribution

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| windeployqt | Bundle Qt DLLs for Windows | ✅ Free | ❌ No | Runs on Windows only |
| macdeployqt | Bundle Qt dylibs for macOS | ✅ Free | ❌ No | Runs on macOS only |
| NSIS / Inno Setup | Windows installer | ✅ Free | ❌ No | Windows only |
| WiX Toolset | MSI installer | ✅ Free | ❌ No | Windows only |
| pkgbuild / productbuild | macOS .pkg installer | ✅ Free | ❌ No | macOS only |
| AppImage / Flatpak / Snap | Linux packaging | ✅ Free | ✅ Yes | |
| CPack | Cross-platform package generation | ✅ Free | ✅ Yes | Part of CMake |
| Apple Developer Program | macOS code signing + notarization | 💰 $99/yr | ❌ No | Required for Gatekeeper |
| Windows EV Code Signing cert | Windows signed installer | 💰 $200–500/yr | ❌ No | Required for SmartScreen bypass |

---

## 3. What I Can Build and Verify (Linux Sandbox)

| Area | Can Build? | Can Test? |
|---|---|---|
| CMake project skeleton + CI | ✅ | ✅ |
| Core C++ library layer (scanner, indexer, job queue) | ✅ | ✅ Unit tests |
| SQLite schema + migrations | ✅ | ✅ |
| EXIF/IPTC/XMP read+write (libexiv2) | ✅ | ✅ |
| RAW decode (libraw) | ✅ | ✅ |
| Thumbnail generation (headless) | ✅ | ✅ |
| FFmpeg video decode + thumbnail extraction | ✅ | ✅ |
| Video trim / stream-copy logic | ✅ | ✅ |
| Qt data models (no UI rendering) | ✅ | ✅ |
| QML UI (compile + headless smoke test) | ✅ | ⚠️ Offscreen only |
| OpenCV face / landmark detection | ✅ | ✅ |
| Map widget (compile) | ✅ | ⚠️ Software render only |
| Linux file watcher (inotify) | ✅ | ✅ |
| AppImage / Flatpak packaging | ✅ | ✅ |
| Windows-specific code | ❌ | ❌ |
| macOS-specific code | ❌ | ❌ |
| Real display / GPU rendering | ❌ | ❌ |
| Code signing | ❌ | ❌ |

---

## 4. Key Paid Items

| Item | Cost | Required for? |
|---|---|---|
| Apple Developer Program | $99/yr | macOS distribution (notarization, App Store) |
| Windows code signing cert | $200–500/yr | SmartScreen trust for Windows installer |
| Qt Commercial License | ~$500+/yr/dev | Only if static linking or commercial support is needed — not required for LGPL path |
| Mapbox / tile hosting | Free tier → paid | Only if commercial tile providers are used instead of OSM |
| GitHub Actions macOS minutes | Consumed from plan | Only for private repos — public repos run free |

Everything else needed to build pimio v1 is **free open-source software**.

---

## 5. GitHub Actions — Cross-Platform CI

### How It Works

GitHub Actions provides hosted virtual machines (runners) for Linux, Windows, and macOS.
When a commit or pull request is pushed, GitHub starts a fresh VM, runs the workflow steps
defined in `.github/workflows/`, and destroys the VM when the job ends.

**For a public repository, all GitHub-hosted runners are free with no minute limits.**
For a private repository, macOS runners cost 10× more minutes than Linux.

### What You Can See

| Access Point | Who | What |
|---|---|---|
| GitHub Actions tab (web UI) | You | All workflow runs, logs, pass/fail status, artifacts |
| GitHub MCP tools | Me (agent) | Same data — I can inspect runs and logs during a session |
| Interactive shell | Limited | A debug tty can be opened manually in a workflow for troubleshooting — not a normal interactive shell |
| Persistent desktop / GUI | ❌ | GitHub Actions is not Codespaces — there is no persistent dev desktop or GUI session |

### GitHub-Hosted Runner Comparison

| Platform | Runner Label | Cost (public repo) | Cost (private repo) | Setup Effort |
|---|---|---|---|---|
| Linux x86_64 | `ubuntu-latest` | ✅ Free | 1× minutes | Trivial |
| Windows x64 | `windows-latest` | ✅ Free | 2× minutes | Low |
| macOS arm64 (M-series) | `macos-latest` | ✅ Free | 10× minutes | Low |
| macOS x64 (Intel) | `macos-13` | ✅ Free | 10× minutes | Low |

### GUI Testing on GitHub Actions

| Test Type | Linux | Windows | macOS |
|---|---|---|---|
| C++ unit tests (no GUI) | ✅ | ✅ | ✅ |
| Qt headless / offscreen tests | ✅ | ✅ | ✅ |
| Qt tests with virtual display (Xvfb) | ✅ | ❌ | ❌ |
| Full real desktop GUI interaction | ❌ | ❌ | ❌ |
| GPU / hardware acceleration | ❌ | ❌ | ❌ |
| Installer UX / Gatekeeper / SmartScreen | ❌ | ❌ | ❌ |

GitHub Actions runners do **not** provide a real logged-in desktop session. GUI automation is
possible on Linux via a virtual framebuffer (`Xvfb`), but macOS and Windows runners do not
easily support this path.

---

## 6. Options for Cross-Platform Build & Test

### Option A: GitHub Actions (Recommended Primary Path)

I write `.github/workflows/` YAML; GitHub runs builds and tests on Linux, Windows, and
macOS automatically on every push or PR. You see the results in the GitHub Actions tab.

**Best for:** compile checks, unit tests, integration tests, packaging, smoke tests.

### Option B: Self-Hosted GitHub Actions Runners on Your Machines

You install the GitHub Actions runner agent on your own Windows or Mac. The runner picks
up jobs from the same workflow YAML and reports results back to GitHub.

| Aspect | Notes |
|---|---|
| Cost | Free (your hardware) |
| Setup | ~30 min per machine |
| Privacy | Source code is pulled to your machine; builds run locally |
| Persistent environment | You control what is installed (Qt, Xcode, certs, etc.) |
| Copilot integration | I write the YAML; your machines execute it |

**Best for:** macOS testing when GitHub minutes are expensive, code signing, and tests
requiring a real display or hardware.

### Option C: MCP with Local Machine Access

An MCP server exposing shell execution on your Windows or Mac would let me invoke
builds and read results directly within a session.

| Aspect | Notes |
|---|---|
| Maturity | Very early — no standard pimio-ready MCP shell server exists today |
| Security | High risk — remote code execution on your machine |
| Usefulness | High if it works — real-time cross-platform feedback |
| Recommended for v1? | No — use GitHub Actions instead |

### Option D: Cloud VMs (Azure, AWS, GCP)

| Platform | Approximate Cost | Notes |
|---|---|---|
| Windows VM | ~$0.10–0.20/hr | Can be registered as a self-hosted Actions runner |
| macOS VM | ~$1.00+/hr | AWS Mac instances require a 24-hour minimum reservation |

**Not recommended when GitHub Actions runners exist for the same purpose at lower complexity.**

---

## 7. Recommended Hybrid Strategy for v1

| Responsibility | Who / Where |
|---|---|
| Core C++ library, CMake setup, data models, headless tests, CI YAML | Me (agent) — built and verified in Linux sandbox |
| Windows and macOS compile + test | GitHub Actions (`windows-latest`, `macos-latest`) triggered by PRs — I write the YAML, GitHub runs it |
| Code signing, notarization, real-hardware tests | Self-hosted runner on your machines (if needed) |
| Visual QML inspection, GPU/hardware acceleration, installer smoke tests | Manual — you on your own machines |
