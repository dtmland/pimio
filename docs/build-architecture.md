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
split is the source of "works in CI, breaks locally" bugs. Product pins and
common Linux prerequisites are now consumed directly from shared inputs;
offline contracts and a real local Linux CI build guard that boundary.

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
  and in every context. CI's LORE server promotion contract acquires
  `loreserver` through this same shared mechanism; local builds can opt in, and
  release archives neither acquire nor package the server.
- **[`packaging/`](../packaging/)** — the launcher, `README.txt`, and
  `pimio-doctor` that ship at the root of every archive. Installed by
  `cmake --install`, so a local install reproduces the released tree.
- **[`tools/build/qt.env`](../tools/build/qt.env)** — Qt version, Qt add-on
  modules, and local aqtinstall version. Both workflows and both local harnesses
  consume this file; the container receives its values as build arguments.
- **[`tools/build/linux-packages.txt`](../tools/build/linux-packages.txt)** —
  the common apt prerequisites for CI, Release builds, and the local container.
  The archive-verification environment intentionally does not consume it.

## The duplicated surface (provisioned per context)

Each context must provision "a machine that can run the shared commands." This is
where differences live. Identical product inputs are loaded rather than copied;
legitimate provisioning differences are documented below.

### Product pins — loaded, not duplicated

| Input | Authoritative source | Consumers |
| --- | --- | --- |
| Qt version/modules; local aqtinstall | `tools/build/qt.env` | Both workflows and local harnesses |
| LORE version/base URL/checksums | `cmake/PimioLore.cmake` | CMake acquisition; Windows pre-download reader; workflow cache keys and Linux reporting use its version |
| Ubuntu image/digest, Linux Qt arch | Linux `pinned.sh` | Container build arguments |
| Windows portable tools, Qt arch, VS components | Windows `pinned.ps1` | Windows host cache and Sandbox |

`tools/build/pins.sh` loads Qt inputs and reads the LORE version. CI and Release
export these values to `GITHUB_ENV` before Qt acquisition and LORE caching.
`tools/build/pins.ps1` reads the same Qt inputs and the version-selected LORE
checksum table, including the base URL, before Windows downloads anything.
Unknown versions or missing/malformed Windows checksums fail closed. The existing
local assertion entry points now reload/validate shared inputs rather than
compare hard-coded copies.

Two bootstrap readers are necessary because a Linux host only needs Bash and a
container engine, while the Windows host/Sandbox can start with PowerShell 5.1
and no CMake. They use existing shell/PowerShell capabilities, not a new parser
dependency. Offline tests compare their output against real CMake evaluation
and mutate the source pins to prove changes propagate.

The Linux image is built through `pimio_prepare_image`, which supplies every
build argument; its build context is the small shared `tools/build/` directory.
There are no independently maintained Containerfile defaults. Use `build.sh`
or `run-studio.sh`, rather than a bare `docker build` without those arguments.

### Why the previous safeguards missed the failures

The old local assertions detected stale Windows LORE checksums but only when a
developer ran the harness. Neither CI nor Release invoked them. A green hosted
build therefore did not demonstrate that either local environment still worked.
The Linux Containerfile also maintained a separate apt list without NASM, even
though CI and Release installed it for libavif's libaom codec. A checklist saying
"review every context" did not test these assumptions.

Both workflows now run `python -m unittest discover -s tests/build -v` before
provisioning. These standard-library tests need no Qt/downloads, exercise both
local pin readers (Windows CI uses PowerShell 5.1), and check shared-input wiring.
CI additionally runs the actual local Linux harness through build, offscreen
tests, and staging, uploading its evidence on failure as well as success.
Windows reader tests do **not** exercise Windows Sandbox, VS installation, or
vendor downloads; changes to those still require a fresh Sandbox run when available.

### Qt acquisition — three mechanisms, same result

The *way* Qt is obtained differs because the starting environments differ; the
*version and modules* obtained come from the shared input above.

| Context | Mechanism |
| --- | --- |
| CI and Release | `jurplel/install-qt-action` |
| Local Linux | `aqtinstall`, baked into the `Containerfile` |
| Local Windows | `aqtinstall`, driven by `prepare.ps1` |

### System package lists — deliberately not identical

The common Linux apt list is shared; **extras and portable-tool sets legitimately
differ**, because each context does a different subset of the work. When adding
a dependency, put shared requirements in the common list and only genuinely
context-specific extras in the consumers.

| Context | List location | Notable contents | Why it differs |
| --- | --- | --- | --- |
| CI build+test | common list + `ci.yml` | xcb libs, `nasm`, `perl`, `ninja-build`, `xvfb` | Builds and runs GUI tests, so it needs `xvfb`; it never deploys, so no `patchelf`. |
| Release build+deploy | common list + `release.yml` | xcb libs, wayland libs, `nasm`, `perl`, `ninja-build`, `patchelf` | Deploys with `cmake --install`, which rewrites ELF RPATH (`patchelf`) and bundles the Wayland plugin; it does not run GUI tests, so no `xvfb`. |
| Release archive verify | `release.yml` | `libgl1`, `libegl1`, `libxcb-cursor0`, `libxkbcommon-x11-0`, `libpulse0` | Deliberately minimal: proves the archive is self-contained on a machine that never built pimio. Mirrors the runtime packages the README asks users to install. |
| Local Linux | common list + `Containerfile` | common codec tools + xcb libs, build-essential, cmake, git, wayland libs, `patchelf`, `xvfb`, python venv for aqt, 7zip, xz | A from-scratch container image that must build, test *and* deploy, so it is the union of the CI and Release needs plus its own toolchain. |

**Perl on Windows.** `libavif` builds its AV1 codec (libaom) from source via CMake
FetchContent. libaom's CMake configuration requires Perl to generate assembly
sources. GitHub-hosted Windows runners (`windows-2025`) come with Strawberry
Perl pre-installed, so CI passes silently; the Windows Sandbox starts from a
bare image and has no Perl. The sandbox toolchain therefore downloads a
[Strawberry Perl portable zip](https://github.com/shogo82148/strawberry-perl-releases)
as a pinned, checksum-verified artifact alongside CMake, Ninja, NASM, and MinGit.
Linux build contexts explicitly request Perl in the common list rather than
depending on incidental packages in a runner or base image.

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
cross-checked between CI and local: platform package managers and portable
archives have different release cadences. They are not product pin authorities.
The local Linux CI job tests the distro-provisioned toolchain; Windows portable
tool changes still need cache/download and Sandbox validation.

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
| `lore.server_promotion` | CI on all platforms; opt-in for local builds with `PIMIO_ENABLE_LORE_SERVER_TESTS=ON` |

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

Shared inputs remove copy-and-paste drift, but neither presets nor static
contracts prove an environment works. CI must exercise the actual local
entrypoint, and Windows Sandbox validation remains a separate requirement.
Context-specific deployment/runtime-library collection and portable tools still
need cross-context review; this change does not unify those platform operations.
When you touch the build, consult
[`.github/copilot-instructions.md`](../.github/copilot-instructions.md) for the
short propagation checklist.
