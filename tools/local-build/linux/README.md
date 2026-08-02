# Local build: Linux

Build, test, and stage pimio in a throwaway Ubuntu 24.04 x86-64 container using
the same Qt, LORE, and CMake presets as CI. The host checkout is mounted
read-only; generated files exist only in the container and the selected results
directory.

See
[../../../docs/plan/pimio-v1-tools-environment.md](../../../docs/plan/pimio-v1-tools-environment.md)
for the design this implements.

## What you need

- An x86-64 Linux host.
- Git.
- Podman 4 or later, or Docker Engine 24 or later.
- Roughly 12 GB of free disk for the image, build, and results.
- Internet access while building the image and acquiring LORE.
- For Studio only, an active X11 or Wayland desktop session.

Install one container engine:

```sh
# Ubuntu/Debian
sudo apt update
sudo apt install podman

# Fedora
sudo dnf install podman
```

Docker installation varies by distribution; follow Docker's official Engine
instructions rather than using the desktop product. Rootless Podman is preferred
when both engines are installed. Use `--engine docker` to choose Docker.

## Run a complete build

From the repository root:

```sh
tools/local-build/linux/build.sh
```

The first run builds the pinned image and downloads Qt. Later runs reuse the
container-engine cache but always build from a clean working copy. The script
runs:

```sh
cmake --preset default -DPIMIO_REQUIRE_LORE=ON
cmake --build --preset default
ctest --preset default
cmake --install build/default --prefix stage
```

Darkroom must pass before the application is staged. A failed configure, build,
or Darkroom run still exports logs and an environment record, but never exports
an application archive.

Useful options:

| Option | Effect |
|---|---|
| `--output <dir>` | Write to a specific host directory. |
| `--engine docker\|podman` | Select an engine instead of preferring Podman. |
| `--image <name> --pull` | Pull and use a prebuilt image, such as an immutable GHCR tag. |
| `--image <name> --use-image` | Use an image already present without building or pulling. |

The default result location is
`build/local-build/linux/<UTC timestamp>/`.

## What you get

| File | Contents |
|---|---|
| `pimio-linux-x64.tar.gz` | Staged application, only when Darkroom passes. |
| `darkroom-junit.xml` | Machine-readable Darkroom results. |
| `Testing/` | CTest logs, including `Temporary/LastTest.log`. |
| `build.log` | Complete container build and test output. |
| `environment.txt` | Commit, image ID, pinned versions, tool versions, commands, and status. |
| `Containerfile` | Exact committed image definition used by the run. |

Install the runtime packages listed in
[../../../docs/supported-platforms.md](../../../docs/supported-platforms.md),
extract the archive, and launch it natively:

```sh
mkdir -p build/local-build/linux/app
tar -xzf build/local-build/linux/<timestamp>/pimio-linux-x64.tar.gz \
    -C build/local-build/linux/app
build/local-build/linux/app/pimio
```

Native launch is the Field Notes environment: it exercises the host graphics
driver, desktop, and window manager. Follow
[../../../docs/plan/manual-testing.md](../../../docs/plan/manual-testing.md).

## Run Studio

Studio executables are build-tree artifacts, so the minimum-host-tools path runs
them in the same image and forwards the real host display:

```sh
tools/local-build/linux/run-studio.sh
```

The script prefers Wayland when a live Wayland socket is available and otherwise
uses X11. Choose explicitly when diagnosing a display path:

```sh
tools/local-build/linux/run-studio.sh --display wayland
tools/local-build/linux/run-studio.sh --display x11
```

Results default to `build/local-build/linux/studio-<UTC timestamp>/` and include
JUnit XML, CTest logs, screenshots, an environment record, and
`pimio-studio-linux-x64.tar.gz`.

The container process maps to your host identity (using keep-ID with rootless
Podman and container root with rootless Docker). X11 forwards the display socket
and, when present, `XAUTHORITY`; Wayland forwards the current
`XDG_RUNTIME_DIR`. The scripts never disable access control with `xhost +`.

## Cleaning up

- Delete a timestamped directory under `build/local-build/linux/`.
- Remove the local image with
  `podman image rm pimio-local-build-linux:ubuntu-24.04-qt-6.8.3` (or the Docker
  equivalent).
- Prune the engine's build cache only when you want the next image build to
  download everything again.

## Troubleshooting

**Permission denied when using Docker** — configure Docker's documented
rootless mode or add your account to the distribution's Docker group, then log
in again. Do not run the script with `sudo`; that creates root-owned results.

**The pinned versions do not match the repository** — `pinned.sh` cross-checks
Qt against `.github/workflows/ci.yml` and LORE against
`cmake/PimioLore.cmake`. Update all authoritative pins together.

**Qt or LORE cannot download** — the image build needs Qt's download service,
and CMake needs GitHub release downloads for LORE. Restore network/proxy access
and rerun; both container engines reuse completed layers and downloads.

**X11 says it cannot connect to the display** — verify `DISPLAY`, the
`/tmp/.X11-unix` socket, and the current user's Xauthority file. Container
Studio supports local Unix-socket displays (`:0`, for example), not TCP-style
SSH forwarding such as `localhost:10.0`; use the native
`tools/field-tests/run-studio.sh` path for an SSH-forwarded display.

**Wayland says the platform plugin cannot initialize** — verify that
`$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY` is a socket and that the container process
uses your user ID. Try `--display x11` when the desktop provides XWayland.

**Podman cannot use the image built by Docker (or vice versa)** — each engine has
its own image store. Build once with the selected engine or use a registry image.

## Changing a pinned version

Update `pinned.sh`, the `Containerfile` defaults, `.github/workflows/ci.yml`, and
`cmake/PimioLore.cmake` together. For a new Ubuntu base, resolve and review the
amd64 image digest before changing it. Pins exist so local results remain
comparable with CI.
