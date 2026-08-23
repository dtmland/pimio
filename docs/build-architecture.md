# Build Architecture: What Is Shared and What Is Not

pimio is built in several contexts — a developer's machine, GitHub-hosted CI,
and the release pipeline — across three platforms (Linux, Windows, macOS). This
document records **what those builds have in common, what is deliberately
different, and why.** Its purpose is to make the shared surface obvious so that a
change to one context is propagated to the others, and to name the places where
*not* sharing is the correct choice.

The short version: **the build is shared; the environment is provisioned per
context.** The commands that configure, build, test, and install pimio come from
one file that every context invokes. The step of *provisioning a machine to run
those commands* is re-implemented per context, because a GitHub runner, a
container, and a Windows Sandbox are genuinely different starting points. That
split is the source of the whole class of "works in CI, breaks locally" (and
vice-versa) bugs, so the duplicated surface is guarded by drift-asserts wherever
it can be.

## The build contexts

| Context | Entry point | Platforms | Purpose |
| --- | --- | --- | --- |
| **CI** | [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) | Linux, Windows, macOS | Build and test every push and PR. |
| **Release** | [`.github/workflows/release.yml`](../.github/workflows/release.yml) | Linux, Windows, macOS | Build, deploy, archive, and verify the shipped binaries on a tag. |
| **Local Linux** | [`tools/local-build/linux/`](../tools/local-build/linux/) | Linux (x86-64) | Reproducible container build/test comparable with CI. |
| **Local Windows** | [`tools/local-build/windows/`](../tools/local-build/windows/) | Windows | Reproducible Windows Sandbox build comparable with CI. |

macOS has no local-build harness; it is exercised only by CI and Release on the
`macos-15` runner.

## The single source of truth (genuinely shared)

These files are authoritative. Every context reads them; none re-implements what
they contain.

- **[`CMakePresets.json`](../CMakePresets.json)** — the actual configure, build,
  test, and install commands. All four contexts invoke the same
  `cmake --preset default`, `cmake --build --preset default`,
  `ctest --preset default`, and `cmake --install build/default`. Its own
  description states it is "shared configuration used locally and in CI on all
  platforms." This is the reason the *build itself* cannot drift between
  contexts.
- **[`CMakeLists.txt`](../CMakeLists.txt),
  [`cmake/PimioLore.cmake`](../cmake/PimioLore.cmake), and
  [`cmake/PimioImageFormats.cmake`](../cmake/PimioImageFormats.cmake)** — the
  dependency requirements and checksum-verified dependency pins. LORE and the
  bundled image decoders are acquired from the same sources on every platform
  and in every context.
- **[`packaging/`](../packaging/)** — the launcher, `README.txt`, and
  `pimio-doctor` that ship at the root of every archive. Installed by
  `cmake --install`, so a local install reproduces the released tree.

## The duplicated surface (provisioned per context)

Each context must provision "a machine that can run the shared commands." This is
where duplication lives. Where a value must be identical across contexts, it is
pinned once and re-read by a drift-assert; where it can legitimately differ, it
is documented below.

### Pinned versions — must be identical, guarded by drift-asserts

The pinned Qt version, the Qt add-on module list, and the LORE version must be
identical everywhere, or a local result stops being comparable with CI and a
release ships bytes CI never verified. They appear in several files:

| Pin | `ci.yml` | `release.yml` | Linux `pinned.sh` / `Containerfile` | Windows `pinned.ps1` |
| --- | --- | --- | --- | --- |
| Qt version | `PIMIO_QT_VERSION` env | `PIMIO_QT_VERSION` env | `PIMIO_QT_VERSION` / `ARG QT_VERSION` | `QtVersion` |
| Qt modules | `modules:` | `qt_modules:` matrix | `PIMIO_QT_MODULES` / `ARG QT_MODULES` | `QtModules` |
| LORE version | `PIMIO_LORE_VERSION` env | `PIMIO_LORE_VERSION` env | `PIMIO_LORE_VERSION` | `LoreVersion` (+ checksums) |

**How drift is prevented.** `ci.yml` and `cmake/PimioLore.cmake` are treated as
authoritative. The local harnesses re-read them and refuse to run on drift:

- Linux: `pimio_assert_pins_match_repository` in
  [`tools/local-build/linux/pinned.sh`](../tools/local-build/linux/pinned.sh),
  called by `build.sh` before every build.
- Windows: `Assert-PimioPinsMatchRepository` in
  [`tools/local-build/windows/pinned.ps1`](../tools/local-build/windows/pinned.ps1),
  called by `prepare.ps1` and `new-sandbox.ps1`.

Both asserts also confirm that **`release.yml` agrees with `ci.yml`** on the Qt
version, the module set, and the LORE version — so the release pipeline cannot
silently diverge from what CI verifies. (This was the gap behind the Qt
Multimedia fix: `pimio` links `Qt6::Multimedia`, the module list was pinned in
only one place, and nothing asserted the others matched.)

### Qt acquisition — three mechanisms, same result

The *way* Qt is obtained differs because the starting environments differ; the
*version and modules* obtained are held identical by the pins above.

| Context | Mechanism |
| --- | --- |
| CI and Release | `jurplel/install-qt-action` |
| Local Linux | `aqtinstall`, baked into the `Containerfile` |
| Local Windows | `aqtinstall`, driven by `prepare.ps1` |

### System package lists — deliberately not identical

The apt lists (Linux) and portable-tool sets are **hand-maintained per context
and legitimately differ**, because each context does a different subset of the
work. They are *not* force-merged; the differences below are intentional. When
you add a build-time or runtime system dependency, decide which of these it
belongs to.

| Context | List location | Notable contents | Why it differs |
| --- | --- | --- | --- |
| CI build+test | `ci.yml` | xcb libs, `nasm`, `ninja-build`, `xvfb` | Builds and runs GUI tests, so it needs `xvfb`; it never deploys, so no `patchelf`. |
| Release build+deploy | `release.yml` | xcb libs, wayland libs, `nasm`, `ninja-build`, `patchelf` | Deploys with `cmake --install`, which rewrites ELF RPATH (`patchelf`) and bundles the Wayland plugin; it does not run GUI tests, so no `xvfb`. |
| Release archive verify | `release.yml` | `libgl1`, `libegl1`, `libxcb-cursor0`, `libxkbcommon-x11-0`, `libpulse0` | Deliberately minimal: proves the archive is self-contained on a machine that never built pimio. Mirrors the runtime packages the README asks users to install. |
| Local Linux | `Containerfile` | superset: build-essential, cmake, git, xcb + wayland libs, `patchelf`, `xvfb`, python venv for aqt, 7zip, xz | A from-scratch container image that must build, test *and* deploy, so it is the union of the CI and Release needs plus its own toolchain. |

**Perl on Windows.** `libavif` builds its AV1 codec (libaom) from source via CMake
FetchContent. libaom's CMake configuration requires Perl to generate assembly
sources. GitHub-hosted Windows runners (`windows-2025`) come with Strawberry
Perl pre-installed, so CI passes silently; the Windows Sandbox starts from a
bare image and has no Perl. The sandbox toolchain therefore downloads a
[Strawberry Perl portable zip](https://github.com/shogo82148/strawberry-perl-releases)
as a pinned, checksum-verified artifact alongside CMake, Ninja, NASM, and MinGit.
Linux CI and the Linux container image both inherit Perl from the Ubuntu base image,
so no explicit Perl install is needed on those paths.

**Bundled image decoders.** AVIF and HEIC are acquired and configured centrally
by `cmake/PimioImageFormats.cmake`; no context installs a system codec package.
The HEIC path builds decoder-only libheif and libde265 shared libraries plus a
dynamic Qt image plugin. This shared-first placement keeps grid composition
identical in CI, Release, and both local environments, while preserving the
LGPL replaceable-library boundary. Release layout checks require the plugin,
both libraries, and their license texts on every platform. A shared CMake-only
source patch namespaces libde265's unused `dist` helper target so it can coexist
with libaom's target of the same name.

The Linux runtime prerequisites a user must supply (not bundled in the archive)
are documented separately in
[supported-platforms.md](supported-platforms.md#linux-runtime-prerequisites).

### Orchestration and tool versions

CI and Release get CMake and Ninja from the runner image or a marketplace action
(`gha-setup-ninja`). The local harnesses pin them explicitly (`Containerfile`
installs distro CMake/Ninja; Windows `pinned.ps1` pins exact CMake/Ninja/NASM/Perl
URLs and checksums, plus `aqtinstall`). These tool versions are **not**
cross-checked between CI and local; they are a smaller, lower-risk duplicated
surface than the Qt/LORE pins.

## What differs across platforms

Even within one context, some things are inherently platform-specific and are
expected to differ:

| Concern | Linux | Windows | macOS |
| --- | --- | --- | --- |
| Compiler / env | GCC | MSVC (`ilammy/msvc-dev-cmd`) | Apple Clang |
| NASM | apt | `choco` (CI/Release), pinned portable zip in local Windows sandbox | `brew` |
| Qt arch id | `linux_gcc_64` | `win64_msvc2022_64` | (default) |
| LORE triple | `x86_64-unknown-linux-gnu` | `x86_64-pc-windows-msvc` | `aarch64-apple-darwin` (no x86-64 build) |
| Deploy runtime | copy Qt + graphics libs, `patchelf` RPATH | Qt deploy tool | `.app` bundle, codesign verify |

The platform mapping for LORE lives in `cmake/PimioLore.cmake`; the supported
matrix and the macOS x86-64 exclusion are in
[supported-platforms.md](supported-platforms.md).

## Test coverage differs by context — on purpose

| Preset | Where it runs |
| --- | --- |
| `default` (offscreen) | CI (all platforms), local Linux, local Windows |
| `default-x11` (Xvfb) | CI Linux only |
| `studio` (native-display GUI) | developer machines / field tests only; no hosted runner has a real display |

CI never runs `studio`; a developer running `studio` exercises tests CI cannot.
This is expected: see [testing.md](testing.md).

## Manual-test assets — shared first

The Field Notes checklist (`tools/manual-test/field-notes.html`) is a
cross-context asset: a developer can use it after a local Linux build, a local
Windows sandbox build, or after extracting a CI/Release-produced archive.

| File | Context | Purpose |
| --- | --- | --- |
| `tools/manual-test/field-notes.html` | **Shared** | The checklist itself. Open from any context. |
| `tools/manual-test/open-field-notes.sh` | **Shared (Unix)** | Opener with `xdg-open` and graceful headless fallback. |
| `tools/local-build/linux/open-field-notes.sh` | Local Linux | Thin wrapper; delegates to the shared opener. |
| `tools/local-build/windows/open-field-notes.ps1` | Local Windows | Windows wrapper; resolves the shared HTML and calls `Start-Process`. |

**Shared-first rationale.** The HTML content and the opener logic (try
`xdg-open`, fall back to printing the URL) have no platform-specific parts.
Placing them under `tools/manual-test/` means they can be used regardless of
which context produced the build artifact. The context-specific wrappers exist
only to handle the platform difference in "how to open a file in a browser"
(`Start-Process` on Windows vs. `xdg-open` on Linux); the rest is shared.

**Pointing at arbitrary artifacts.** Pass `--build-dir <path>` to the opener
scripts (or set `PIMIO_STAGE_DIR`) to tell the script where the staged
application lives. The Field Notes HTML itself is not tied to any particular
output directory; it works the same regardless of whether the artifacts came from
a local build, a CI run, or an extracted release archive.

**Auto-open behaviour.**

| Context | Mechanism | Headless fallback |
| --- | --- | --- |
| Local Linux | `xdg-open` (via `open-field-notes.sh`) | Prints `file://` URL |
| Local Windows (sandbox) | `Start-Process` (via `open-field-notes.ps1`) | n/a — sandbox always has a desktop |
| CI / Release | Not automatically opened | Developer opens manually from `tools/manual-test/field-notes.html` |

## Summary: the blast radius

Anything covered by a preset or a drift-assert stays in lockstep across contexts.
Anything provisioned per context by a hand-maintained list is a potential drift
source. The largest remaining hand-maintained surface is the system package
lists, which are intentionally different (above) and therefore documented rather
than auto-reconciled. When you touch the build, consult
[`.github/copilot-instructions.md`](../.github/copilot-instructions.md) for the
short propagation checklist.
