# pimio

[![CI](https://github.com/dtmland/pimio/actions/workflows/ci.yml/badge.svg)](https://github.com/dtmland/pimio/actions/workflows/ci.yml)

**pimio** is a local-first photo and video organizer for repairing and
maintaining chronological media libraries. It aims for a simple, Picasa-like
browsing experience on top of durable, version-controlled storage.

Built with C++20, Qt 6, and QML. Storage is backed by
[LORE](https://github.com/EpicGames/lore) (`liblore`) in-process: one Library is
one portable repository with stable identity, history, and managed originals.

| Audience | Jump to |
| --- | --- |
| **Users** — install, run, libraries, troubleshooting | [For users](#for-users) |
| **Sysadmins** — platforms, deploy, diagnose, release verify | [For sysadmins](#for-sysadmins) |
| **Developers** — build, test, architecture, contributing | [For developers](#for-developers) |

---

## Status

pimio is under active development toward a 1.0.0 standalone desktop release.
Architecture and library lifecycle foundations through increment **7.9** are in
place (durable store, scan/watch, metadata, browser, library manager, backup
and restore, offline-to-server promotion feasibility). Product workflows still
ahead include non-destructive save/recipes, timestamp repair, richer video
tools, location, and release-candidate hardening. See
[docs/plan/progress.md](docs/plan/progress.md).

Pre-release binary archives are published from version tags. Expect alpha rough
edges: no code signing/notarization yet, and server promotion remains an
accepted alpha risk while an upstream interrupted-push recovery issue is open.

---

## For users

### What pimio does

- Organize photos and videos around **time**, even when capture metadata is
  missing, incomplete, or wrong (repair tools are still landing for 1.0).
- Treat a **Library** as the only storage concept you need: create, open,
  rename, move, back up, and restore it as one portable object.
- **Import** media into the library. Originals are copied into managed storage;
  deleting a source folder later does not remove committed items.
- Browse a virtualized grid while a large library is still scanning; thumbnails
  and search indexes rebuild from the durable store if caches are cleared.
- Keep advanced storage details out of the way. LORE version control is the
  foundation, not the day-to-day UI vocabulary.

Longer product vision:
[docs/plan/pimio.md](docs/plan/pimio.md). Release goal:
[docs/plan/pimio-v1.md](docs/plan/pimio-v1.md).

### Supported platforms (product baseline)

| Platform | Baseline |
| --- | --- |
| Windows | Windows 10 22H2 and Windows 11, x86-64 |
| macOS | macOS 14+, **Apple Silicon (arm64) only** |
| Linux | Ubuntu 22.04 LTS and later, x86-64, X11 and Wayland |

Details and provisional notes:
[docs/supported-platforms.md](docs/supported-platforms.md).

### Install a release archive

1. Open the latest [GitHub release](https://github.com/dtmland/pimio/releases).
2. Download the archive for your OS:
   - `pimio-<tag>-Linux-binaries.tar.gz`
   - `pimio-<tag>-Windows-binaries.zip`
   - `pimio-<tag>-macOS-binaries.tar.gz`
3. Extract it somewhere you can write (for example `~/pimio` or
   `C:\Users\<you>\pimio`).
4. Start the **launcher** in that folder:
   - Linux / macOS: `./pimio`
   - Windows: `pimio.bat`

Each archive also includes a platform `README.txt` with the same run steps and
any packages your OS still needs. Keep the extracted tree together; moving the
binary alone out of the tree breaks Qt plugin resolution.

#### Windows notes

- Builds are not code-signed. SmartScreen may warn; use **More info → Run
  anyway** if you trust the download.
- If startup fails on missing `VCRUNTIME140.dll` / `MSVCP140.dll`, run
  `bin\vc_redist.x64.exe` once.

#### macOS notes

- Arm64 only. There is no Intel build for v1.
- Builds are ad-hoc signed, not notarized. After download, clear quarantine once
  you trust the archive:

  ```sh
  xattr -dr com.apple.quarantine ~/pimio/pimio.app
  ```

  Or right-click the app → **Open** and confirm.

#### Linux notes

Qt is bundled, but a few session/graphics libraries come from the distribution:

```sh
# Debian / Ubuntu
sudo apt install libxcb-cursor0 libgl1 libegl1 libxkbcommon-x11-0 libpulse0

# Fedora
sudo dnf install xcb-util-cursor mesa-libGL mesa-libEGL libxkbcommon-x11 pulseaudio-libs
```

Without `libxcb-cursor0`, startup often fails with a Qt `xcb` platform plugin
error even though the plugin file is present.

### Everyday use

- Prefer the **Library Manager** in the app for create / open / rename / move /
  backup / restore. A library keeps a stable id even when its folder moves.
- Optional compatibility CLI roots still work for import discovery:

  ```sh
  ./pimio --library /path/to/photos --library /path/to/more
  ```

  Paths given this way are import locators, not library identity.
- Backup produces a versioned `.pimio-backup` of the durable store. Restore
  rebuilds indexes and caches from that store.
- Check version without a window:

  ```sh
  ./pimio --version          # pimio.bat --version on Windows
  ```

### When something goes wrong

1. Close pimio.
2. Run the diagnostic next to the launcher and attach its report to an issue:

   ```sh
   ./pimio-doctor                  # Linux / macOS
   powershell -ExecutionPolicy Bypass -File .\pimio-doctor.ps1   # Windows
   ```

   It writes `pimio-doctor-report.txt`, prints a short **LIKELY CAUSE** summary,
   and exits non-zero on hard problems. Reports are intended to be safe to paste
   into a public issue.
3. Optional headless smoke:

   ```sh
   QT_QPA_PLATFORM=offscreen ./pimio --self-check
   ```

Report problems at <https://github.com/dtmland/pimio/issues>.

### Reset rebuildable local state (safe)

Derived data (SQLite projection, job queue paths, thumbnail caches, settings)
can be deleted without removing library originals. Close pimio first, then:

```sh
# Linux
rm -rf "${XDG_STATE_HOME:-$HOME/.local/state}/pimio"
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/pimio"
rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/pimio"

# macOS
rm -rf "$HOME/Library/Application Support/pimio"
rm -rf "$HOME/Library/Caches/pimio"
rm -f  "$HOME/Library/Preferences/io.pimio.pimio.plist"
```

```powershell
# Windows (PowerShell)
Remove-Item -Recurse -Force "$env:LOCALAPPDATA\pimio" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "$env:APPDATA\pimio" -ErrorAction SilentlyContinue
```

Restart pimio and let it rebuild indexes and thumbnails.

---

## For sysadmins

### What you are deploying

Release archives are **self-contained application trees**, not OS packages.
Extract in place, run the launcher, keep the layout intact. Sources for
launchers, per-platform `README.txt`, and `pimio-doctor` live in
[packaging/](packaging/) and are installed by `cmake --install`, so a local
install matches a published release layout.

### Archive layout

```
README.txt        how to run it, and what the platform still needs
pimio             launcher (pimio.bat on Windows)
pimio-doctor      diagnostic script (pimio-doctor.ps1 on Windows)
bin/              executable, qt.conf, and on Windows the Qt DLLs
lib/              bundled Qt and system libraries (Linux)
plugins/          Qt plugins, including plugins/platforms
qml/              QML modules the application imports
translations/     Qt's own translations
```

macOS ships `pimio.app` instead of the split `bin/` / `lib/` / `plugins/` /
`qml/` tree; everything lives inside the bundle.

Start through the launcher. It changes to its own directory first, so it works
from any working directory. On Linux it selects Wayland or xcb to match the
session. Running `bin/pimio` directly only works from the extracted tree because
`bin/qt.conf` uses a prefix relative to the executable.

### Runtime prerequisites

| Platform | Must provide |
| --- | --- |
| Linux | `libxcb-cursor0`, GL/EGL dispatch, `libxkbcommon-x11-0`, PulseAudio client (`libpulse0`) from the distro — see user section |
| Windows | VC++ 2015–2022 x64 redistributable (`bin\vc_redist.x64.exe` in the archive) |
| macOS | Arm64 host; clear Gatekeeper quarantine for unsigned downloads |

Display: normal desktop sessions (X11 or Wayland on Linux). For remote verify
without a display, the offscreen plugin is shipped:

```sh
QT_QPA_PLATFORM=offscreen ./pimio --self-check
./pimio-doctor
```

### Data locations and durability

| Kind | Role | Safe to delete? |
| --- | --- | --- |
| LORE repository (library folder) | Durable originals, canonical records, history, identity (`records/.pimio-library.json`) | **No** — this is the library |
| `.pimio-backup` | Checksummed single-file backup of the durable store | Keep off-box copies |
| Projection / job / thumbnail caches | Rebuildable derived state under app state/cache paths | Yes |
| `pimio.conf` / platform settings | User preferences | Yes (resets settings) |

v1 stores **managed originals** inside the library repository. Import paths are
provenance only. Network and FAT-family volumes are best-effort and should
degrade visibly; preferred filesystems are NTFS, APFS, ext4, btrfs, and xfs.

Library lifecycle (create, open, close, rename, move, backup, restore) goes
through the in-process Library Manager / service boundary. Backup holds the
repository writer lock; restore verifies SHA-256 digests before publishing and
rebuilds derived state. See
[docs/library-model.md](docs/library-model.md) and
[docs/library-service-api.md](docs/library-service-api.md).

### Promotion (alpha)

A local library can be promoted toward a LORE server without changing library
identity. Promotion is enabled for alpha with known upstream recovery limits
(interrupted initial-push retry). Do not treat multi-user server hosting as a
supported v1 product yet; headless **pimio Server** is a later milestone
([docs/decisions/0006-local-first-lore-topology.md](docs/decisions/0006-local-first-lore-topology.md)).

### Cutting and verifying releases

Releases are automated from tags matching `v*` or bare semver (for example
`0.1.1`):

```sh
git tag 0.1.1
git push origin 0.1.1
```

[`.github/workflows/release.yml`](.github/workflows/release.yml) builds on
Linux, Windows, and macOS, bakes the tag (without a leading `v`) into
`--version`, and uploads:

- `pimio-<tag>-source-snapshot.tar.gz`
- `pimio-<tag>-Linux-binaries.tar.gz`
- `pimio-<tag>-Windows-binaries.zip`
- `pimio-<tag>-macOS-binaries.tar.gz`

Before publish, each archive is extracted on a **clean** runner that never built
pimio, then checked with `pimio-doctor` and
`QT_QPA_PLATFORM=offscreen ./pimio --self-check`. Version mismatch fails the
job. Local builds report the checked-in `-dev` default from
`PIMIO_RELEASE_VERSION` in `CMakeLists.txt` and do not consult git tags at
configure time.

### Diagnostics checklist

1. `pimio-doctor` (layout, unresolved libs, `qt.conf`, plugins, launch capture).
2. `pimio --self-check` under `offscreen` (or real session for GUI issues).
3. Confirm distro/runtime packages (Linux cursor/GL, Windows VC++ redist, macOS
   quarantine).
4. If the UI is corrupt after an upgrade, clear rebuildable state only (see
   users) — not the library repository.
5. File issues with the doctor report attached:
   <https://github.com/dtmland/pimio/issues>.

---

## For developers

### Repository map

| Path | Role |
| --- | --- |
| `src/core/` | UI-independent types and boundaries (`pimio::core`; Qt Core only) |
| `src/lore/` | LORE durable store adapter (`lore.h` private to this tree) |
| `src/metadata/` | Built-in metadata readers (no third-party metadata library) |
| `src/scan/` | Incremental scan and media identity |
| `src/projection/` | Rebuildable SQLite query cache and job queue |
| `src/settings/` | Stored (`pimio.conf`) and session settings |
| `src/thumbnail/`, `src/watch/`, `src/browser/` | Derivatives, FS watch, models/UI helpers |
| `src/app/` | Qt shell, Library Manager, QML (`Pimio` module) |
| `tests/` | Darkroom / Studio tests and fixtures |
| `packaging/` | Shipped launcher, README.txt, doctor |
| `cmake/` | Pinned LORE and image-format acquisition |
| `tools/local-build/` | Reproducible Linux container and Windows Sandbox builds |
| `docs/` | All project documentation (root keeps README + build entrypoints only) |

Conventions: [docs/conventions.md](docs/conventions.md).

### Architecture in one page

Three layers stay separated on purpose:

1. **UI / application** — QML browser, library manager dialogs, session facade.
2. **Services** — scan, metadata, projection, thumbnails, jobs, library
   lifecycle API shaped for a future client/server split.
3. **LORE storage** — durable content, history, identity, managed originals.

Durable vs rebuildable: the LORE repository is source of truth; SQLite indexes,
thumbnails, and job DBs are disposable and rebuilt from the repository.
Identity lives in `records/.pimio-library.json` and survives move/copy/restore.

Key decisions:

- [0001 — LORE durable store](docs/decisions/0001-lore-durable-store.md)
- [0002 — Built-in metadata reader](docs/decisions/0002-metadata-adapter.md)
- [0003 — Settings and view controls](docs/decisions/0003-settings-and-view-controls.md)
- [0004 — Progressive scan and thumbnail retention](docs/decisions/0004-progressive-scan-and-thumbnail-retention.md)
- [0005 — Managed vs referenced originals](docs/decisions/0005-managed-versus-referenced-originals.md)
- [0006 — Local-first LORE topology](docs/decisions/0006-local-first-lore-topology.md)

Library contracts:
[docs/library-model.md](docs/library-model.md),
[docs/library-service-api.md](docs/library-service-api.md).

### Prerequisites

- CMake >= 3.24, Ninja, C++20 compiler
- Qt 6 (>= 6.4 to configure; **6.8.3** is the version CI verifies)
- Network on first configure to fetch pinned LORE (and image decoders) unless
  already cached under `.cache/`
- Optional: Xvfb on Linux for the X11 test preset

Platform policy and dependency licenses:
[docs/supported-platforms.md](docs/supported-platforms.md),
[docs/dependency-bom.md](docs/dependency-bom.md).

### Build, test, install

Same preset names locally and in CI:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
cmake --install build/default --prefix stage   # optional; matches release tree
```

Linux X11 / Studio approximation under Xvfb:

```sh
xvfb-run -a --server-args="-screen 0 1280x1024x24" ctest --preset default-x11
```

Useful options:

| CMake option | Purpose |
| --- | --- |
| `PIMIO_WITH_LORE` (default ON) | Acquire LORE; tests skip with a reason if missing |
| `PIMIO_REQUIRE_LORE=ON` | Fail configure when LORE cannot be acquired (CI / local-build) |
| `PIMIO_ENABLE_LORE_SERVER_TESTS=ON` | Acquire `loreserver` and run promotion contract |
| `PIMIO_ENABLE_FAILING_SELFTEST=ON` | Negative control that CI reports failures |

### Testing tiers

| Tier | Name | Where |
| --- | --- | --- |
| A | **Darkroom** | Headless `ctest --preset default` on every CI push (Linux, Windows, macOS) |
| B | **Studio** | Real-display GUI tests in `tests/studio/`; Linux CI under Xvfb; desktops via `tools/field-tests/run-studio.sh` / `.ps1` |
| C | **Field Notes** | Manual matrix in [docs/plan/manual-testing.md](docs/plan/manual-testing.md) |

Full guide: [docs/testing.md](docs/testing.md).

### Reproducible local environments

The **build commands** are shared (`CMakePresets.json`); **environment
provisioning** is per context. Prefer these harnesses when matching CI:

- Linux container: [tools/local-build/linux/README.md](tools/local-build/linux/README.md)
  (`tools/local-build/linux/build.sh`)
- Windows Sandbox: [tools/local-build/windows/README.md](tools/local-build/windows/README.md)

Shared vs per-context pins, packages, and drift asserts:
[docs/build-architecture.md](docs/build-architecture.md).

When you change a dependency, Qt module, compiler flag, or package list, update
**every** relevant context: `.github/workflows/ci.yml`,
`.github/workflows/release.yml`, `tools/local-build/linux/`,
`tools/local-build/windows/`, and shared CMake modules — then confirm drift
asserts still pass.

### CI and PR expectations

- CI: [`.github/workflows/ci.yml`](.github/workflows/ci.yml) on
  `ubuntu-24.04`, `windows-2025`, `macos-15` (arm64).
- PRs should name the implementation increment, list accepting CTest names, and
  link a green three-platform run. A skipped platform is an incomplete
  increment ([docs/conventions.md](docs/conventions.md)).
- Progress board: [docs/plan/progress.md](docs/plan/progress.md).
- Implementation plan: [docs/plan/pimio-v1-implementation.md](docs/plan/pimio-v1-implementation.md).

### Useful documents

**Planning**

- [Product vision](docs/plan/pimio.md)
- [1.0.0 release plan](docs/plan/pimio-v1.md)
- [1.0.0 implementation plan](docs/plan/pimio-v1-implementation.md)
- [Tools / environment / CI strategy](docs/plan/pimio-v1-tools-environment.md)
- [2.0.0 release plan](docs/plan/pimio-v2.md)
- [Picasa capability reference](docs/plan/picasa.md) /
  [modern mapping](docs/plan/modern-picasa.md)

**Policy and reference**

- [Repository conventions](docs/conventions.md)
- [Build architecture](docs/build-architecture.md)
- [Testing tiers](docs/testing.md)
- [Supported platforms](docs/supported-platforms.md)
- [Dependency BOM](docs/dependency-bom.md)
- [Library model](docs/library-model.md) /
  [Library service API](docs/library-service-api.md)

---

## License and third-party notices

pimio redistributes Qt (LGPL dynamic linking), LORE (MIT, plus its
`THIRD-PARTY-NOTICES.txt`), and image-decode stacks (AVIF/HEIC) as recorded in
[docs/dependency-bom.md](docs/dependency-bom.md). Legal review remains a release
gate. Ship license texts with binaries; do not replace upstream notice files
with summaries.
