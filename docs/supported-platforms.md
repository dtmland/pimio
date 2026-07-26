# Supported Platform Policy

Status: **stub established in Increment 0.** Values here are the current
working baseline used by the build and CI. They must be reviewed and confirmed
before the 1.0.0 release; see
[pimio-v1-implementation.md](plan/pimio-v1-implementation.md), planning item 2.

CI runner images are test environments. They do not by themselves define the
supported product matrix.

## Product baseline

| Item | Baseline | Confirmed? |
| --- | --- | --- |
| Qt | 6.8.3 (pinned in CI); CMake accepts >= 6.4 | Provisional |
| C++ standard | C++20 | Provisional |
| Build system | CMake >= 3.24 with CMake Presets, Ninja generator | Provisional |
| Windows | Windows 10 22H2 and Windows 11, x86-64 | Provisional |
| macOS | macOS 14 and later, arm64 (Apple Silicon) only | **Confirmed for v1** |
| Linux | Ubuntu 22.04 LTS and later, x86-64, X11 and Wayland | Provisional |
| Linux arm64 | Buildable, not verified by CI | Provisional |
| Filesystems | NTFS, APFS, ext4, btrfs, xfs. Network and FAT-family volumes are best-effort and must degrade visibly. | Provisional |

### macOS x86-64 is not supported in v1

The durable store depends on LORE, and the upstream project publishes no
macOS x86-64 build: release 0.8.5 ships `aarch64-apple-darwin` only, alongside
`x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu-neoverse-512tvb`, and
`x86_64-pc-windows-msvc`. Supporting Intel Macs would mean building LORE from
source as part of pimio's release pipeline, which is disproportionate for the
platform's remaining share. Revisit only if upstream starts publishing the
artifact.

`cmake/PimioLore.cmake` encodes this: on macOS x86-64 it reports that no
artifact exists rather than guessing a triple, so a build there configures
cleanly with LORE disabled instead of failing obscurely.

### Linux arm64

LORE publishes `aarch64-unknown-linux-gnu-neoverse-512tvb`, so the dependency
does not block the target and `cmake/PimioLore.cmake` maps to it. Nothing in
hosted CI exercises it, so it stays provisional: buildable, unverified, and not
a release target for v1 until a runner exists.

The CMake minimum Qt version is intentionally lower than the pinned CI version
so contributors can build with a distribution Qt. Only the pinned version is
verified by CI.

### Linux runtime prerequisites

The Linux archive bundles Qt, but a few libraries must come from the
distribution because they are tied to the graphics stack and the session:

| Package (Debian, Ubuntu) | Provides | Why |
| --- | --- | --- |
| `libxcb-cursor0` | `libxcb-cursor.so.0` | Required by the xcb platform plugin from Qt 6.5 onwards. Without it the plugin is found but cannot be loaded, and startup fails with `Could not load the Qt platform plugin "xcb" in "" even though it was found.` |
| `libgl1`, `libegl1` | GL and EGL dispatch | Vendor-neutral entry points that must match the installed driver. |
| `libxkbcommon-x11-0` | keymap handling | Loaded by the xcb platform plugin. |

The equivalents on Fedora are `xcb-util-cursor`, `mesa-libGL`, `mesa-libEGL`,
and `libxkbcommon-x11`.

### Wayland and X11

The release archive ships both platform plugins: `qtwayland` is installed in
the release build, so `plugins/platforms` carries the Wayland plugins alongside
`libqxcb.so`. The bundled launcher picks Wayland on a Wayland session when the
plugin is present and falls back to xcb through XWayland otherwise. Setting
`QT_QPA_PLATFORM` overrides the choice.

Only X11 is exercised by CI, through the `default-x11` CTest preset under Xvfb.
Wayland remains provisional: the plugin ships and the archive is verified to
start under the offscreen platform, but no hosted runner provides a real
Wayland compositor.

## CI runner images

Pinned labels, chosen to avoid silent `*-latest` image changes:

| Job | Runner label |
| --- | --- |
| Build and test (Linux) | `ubuntu-24.04` |
| Build and test (Windows) | `windows-2025` |
| Build and test (macOS) | `macos-15` (arm64) |

## Not covered by hosted CI

Real desktop sessions, GPU and hardware-accelerated decode, code signing,
notarization, Gatekeeper, and SmartScreen behavior are not proven by
GitHub-hosted runners. They require documented manual tests or labeled
self-hosted runners before release.

## Open decisions

- Confirm the pinned Qt minor release for 1.0.0 and whether an LTS is required.
- Confirm whether Linux arm64 becomes a release target, which requires a runner
  that can execute the LORE-backed tests.
- Confirm the minimum Windows and Linux versions against the chosen Qt release.
