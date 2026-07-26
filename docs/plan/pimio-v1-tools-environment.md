# pimio v1 — Tools, Environment & CI Strategy

This is an environment and options inventory, not an implementation sequence.
See [pimio-v1-implementation.md](pimio-v1-implementation.md) for the staged
delivery gates, acceptance tests, and required CI evidence.

Sections 1 to 4 remain an inventory of what is available. **Section 5 onward now
describes CI as it is actually built**, not as it was planned: `.github/workflows/ci.yml`
and [../supported-platforms.md](../supported-platforms.md) are the authoritative
record, and this document is a reader's guide to them. Anything still undecided
is called out as such.

## 1. My Sandbox Environment

I (GitHub Copilot agent) run in a standard **Ubuntu Linux x86_64** environment. The table below
shows what is and is not available to me directly.

| Capability | Available? | Notes |
|---|---|---|
| apt package manager | ✅ Yes | Full access |
| GCC 13+ / Clang 17+ / CMake / Ninja | ✅ Yes | Standard Linux build toolchain |
| Qt 6 (via apt) | ✅ Yes | `qt6-base-dev`, `qt6-declarative-dev` give Qt 6.4.2, which builds and tests the repository. Only the QML runtime modules the app smoke test needs are missing. |
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
| libexiv2 | EXIF/IPTC/XMP read+write | ✅ Free (GPL/LGPL) | ✅ Yes | GPL — review linking strategy before distribution. Not used for the read path: Increment 5 reads container headers with parsers owned by this project, see [../decisions/0002-metadata-adapter.md](../decisions/0002-metadata-adapter.md) |
| libraw | RAW image decode | ✅ Free (LGPL/CDDL) | ✅ Yes | |
| libjpeg-turbo | JPEG decode / lossless transform | ✅ Free | ✅ Yes | Needed for lossless EXIF-preserving rotation |
| FFmpeg (LGPL build) | Video decode, thumbnails, trim | ✅ Free | ✅ Yes | Avoid GPL codecs to keep distribution simpler |
| x264 / x265 | H.264 / HEVC encode | ⚠️ Complex | ✅ Yes | Both GPL; HEVC also carries patent licensing considerations |
| OpenImageIO / stb_image | Supplemental image decode | ✅ Free | ✅ Yes | |

### Storage & Versioning

| Tool | Purpose | Free? | My Env? | Notes |
|---|---|---|---|---|
| SQLite | Local cache / index | ✅ Free (Public Domain) | ✅ Yes | |
| LORE | Versioned ground-truth store | ✅ Free (MIT) | ✅ Yes | Adopted in Increment 2; see [../decisions/0001-lore-durable-store.md](../decisions/0001-lore-durable-store.md). Acquired by `cmake/PimioLore.cmake` |

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
| EXIF/IPTC/XMP read (built-in parsers) | ✅ | ✅ Golden tests against the committed fixture corpus |
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

## 5. GitHub Actions — Cross-Platform CI (in place)

### What runs today

`.github/workflows/ci.yml` runs one `build-and-test` job per platform on every
pull request, every push to `main`, and on manual dispatch. The three jobs are
independent (`fail-fast: false`), so one platform failing still shows the result
of the other two.

| Job name | Runner label | Notes |
|---|---|---|
| Build and test (Linux) | `ubuntu-24.04` | Also runs the X11 test pass under Xvfb |
| Build and test (Windows) | `windows-2025` | MSVC developer environment via `ilammy/msvc-dev-cmd` |
| Build and test (macOS) | `macos-15` | arm64. macOS x86-64 is not a v1 target |

Labels are pinned rather than `*-latest`, so a runner image change is a
deliberate commit instead of a surprise. The labels are also recorded in
[../supported-platforms.md](../supported-platforms.md), which is the policy
document; this table is a convenience copy.

### What each job does

1. Checks out the repository (`actions/checkout@v5`).
2. Installs native dependencies: X11/XCB, GL, Ninja, and Xvfb on Linux;
   `seanmiddleditch/gha-setup-ninja` on Windows and macOS.
3. Installs the pinned Qt release with `jurplel/install-qt-action`, cached per
   runner and version. The pinned version lives in one place, the workflow's
   `PIMIO_QT_VERSION`.
4. Restores the cached LORE artifact, keyed by runner and `PIMIO_LORE_VERSION`.
5. Configures with `cmake --preset default -DPIMIO_REQUIRE_LORE=ON`. LORE is
   optional locally but required in CI: a job that quietly skipped the
   durable-store tests would report green without running the evidence an
   increment depends on.
6. Prints the acquired LORE version from `build/default/lore-acquired.txt`, so
   every run states what it actually used.
7. Builds, then runs `ctest --preset default` (offscreen) on all three
   platforms, writing JUnit XML.
8. On Linux only, runs `ctest --preset default-x11` under `xvfb-run`, which is
   the pass that exercises a real X server.
9. Uploads `build/default/test-results/` and `build/default/Testing/` as
   `test-results-<platform>`, always, including on failure.

### Proving that failures are reported

`workflow_dispatch` accepts a `failing_selftest` input. Setting it configures
with `-DPIMIO_ENABLE_FAILING_SELFTEST=ON`, which registers a CTest case that
always fails. It exists to demonstrate that each platform job really reports a
red result rather than silently skipping tests — a green matrix means nothing
until the red case has been seen.

### What You Can See

| Access Point | Who | What |
|---|---|---|
| GitHub Actions tab (web UI) | You | All workflow runs, logs, pass/fail status, artifacts |
| GitHub MCP tools | Me (agent) | Same data — I can inspect runs, jobs, and logs during a session |
| Uploaded artifacts | Both | `test-results-<platform>`: CTest JUnit XML and `Testing/` logs, kept 14 days |
| Interactive shell | Limited | A debug tty can be opened manually in a workflow for troubleshooting — not a normal interactive shell |
| Persistent desktop / GUI | ❌ | GitHub Actions is not Codespaces — there is no persistent dev desktop or GUI session |

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
possible on Linux via a virtual framebuffer (`Xvfb`), which is why only the Linux job has a
second X11 pass. macOS and Windows runners do not easily support this path, so anything in
the ❌ rows needs a documented manual test or a self-hosted runner before release.

### Runner behaviour worth knowing

These are quirks observed on the actual runners, recorded so they are not
rediscovered as bugs:

- **Windows rewrites line endings.** GitHub's Windows runners default to
  `core.autocrlf=true`, which would rewrite LF-only text fixtures to CRLF on
  checkout and break byte-exact and hash-exact comparisons. The repository's
  root `.gitattributes` marks the fixture corpus binary (`-text`) to prevent it.
- **Windows hides test stdout.** QtTest executable output is not captured into
  the Windows `LastTest.log`, so a blank Windows test log is a capture quirk
  rather than a crash. Read per-test detail from the Linux or macOS artifact of
  the same run.
- **Hidden files behave differently.** `QDir::Files` skips dot-prefixed files on
  Unix but not on Windows, where hidden is an attribute. Configuration files
  inside a walked directory therefore change results on Windows only.
- **macOS x86-64 has no runner and no LORE build.** `macos-15` is arm64;
  Intel macOS is out of scope for v1 for the reason recorded in
  [../supported-platforms.md](../supported-platforms.md).

### Still open

- Branch protection must require all three jobs. That is a repository setting,
  not something the workflow can assert.
- No hosted runner exercises Linux arm64, so it stays buildable and unverified.
- Packaging, signing, and notarization jobs do not exist yet; they belong to
  Increment 12.

---

## 6. Options for Cross-Platform Build & Test

### Option A: GitHub Actions — **chosen, and in place**

`.github/workflows/ci.yml` builds and tests on Linux, Windows, and macOS on every push
and pull request, as described in section 5. Results appear in the GitHub Actions tab.

**Best for:** compile checks, unit tests, integration tests, packaging, smoke tests.

The remaining options below were not adopted. They are kept because they are the
answers to problems hosted CI genuinely cannot solve, and those problems arrive
at packaging time.

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
| Windows and macOS compile + test | GitHub Actions on pinned supported runner images, triggered by PRs |
| Code signing, notarization, real-hardware tests | Self-hosted runner on your machines (if needed) |
| Visual QML inspection, GPU/hardware acceleration, installer smoke tests | Manual — you on your own machines |

The first two rows are implemented: pinned runner labels selected by the
supported-platform policy, three independently visible platform jobs, and a
pinned Qt version. Requiring all three jobs through branch protection is still a
repository setting that has to be turned on outside the workflow.

Hosted-runner availability, labels, pricing, and preinstalled software can
change. `.github/workflows/ci.yml`, `CMakePresets.json`, and
[../supported-platforms.md](../supported-platforms.md) are the authoritative
reproducible configuration; this document is not.
