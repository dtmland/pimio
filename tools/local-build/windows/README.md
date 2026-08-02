# Local build: Windows

Build, test, and stage pimio for Windows in a throwaway Windows Sandbox, using
the same pinned Qt, LORE, and CMake presets as CI. Your machine keeps no
compiler, SDK, or build output: everything is installed inside the sandbox and
discarded when you close it.

See [../../../docs/plan/pimio-v1-tools-environment.md](../../../docs/plan/pimio-v1-tools-environment.md)
for the design this implements.

## What you need

- Windows 10/11 **Pro, Enterprise, or Education** (Windows Sandbox is not
  available on Home).
- Hardware virtualization enabled in firmware.
- The **Windows Sandbox** optional feature enabled:

  ```powershell
  Enable-WindowsOptionalFeature -FeatureName 'Containers-DisposableClientVM' -All -Online
  ```

  Reboot afterwards.
- Python 3 on PATH (only to run `aqtinstall`, which downloads Qt).
- Roughly 25 GB of free disk for the cache and the sandbox.

## Prepare the cache once

```bat
tools\local-build\windows\prepare.bat
```

This checks the host prerequisites, then downloads and checksum-verifies the
pinned Visual Studio Build Tools bootstrapper, CMake, Ninja, LORE, and Qt into
`.cache\local-build\windows\` (git-ignored). Downloads resume, so re-running
after an interruption is cheap, and re-running when nothing changed does
nothing.

If you prefer to launch the PowerShell scripts directly, either invoke them with
`-ExecutionPolicy Bypass`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\local-build\windows\prepare.ps1
```

or relax PowerShell's policy for your own account/session before running the
`.ps1` files.

Useful flags:

| Flag | Effect |
|---|---|
| `-CheckOnly` | Verify prerequisites and report what is missing; download nothing. |
| `-SkipQt` | Skip the Qt download (the slowest step) when it is already cached. |
| `-Force` | Re-download and re-verify everything. |
| `-CacheRoot <path>` | Use a cache outside the checkout, for example on another drive. |

The Visual Studio Build Tools are downloaded from Microsoft under Microsoft's
license and are never repackaged or published by this repository.

## Run a build

```bat
tools\local-build\windows\new-sandbox.bat
```

This writes a `.wsb` configuration into a timestamped results directory and
launches Windows Sandbox. The sandbox maps:

| Sandbox path | Host path | Access |
|---|---|---|
| `C:\pimio\source` | your checkout | read-only |
| `C:\pimio\cache` | the prepared cache | read-only |
| `C:\pimio\results` | `build\local-build\windows\<timestamp>\` | read-write |

On logon the sandbox runs `sandbox-bootstrap.ps1`, which installs the tools from
the cache, copies the source to `C:\pimio\work` (your checkout is never
written to), and then runs the standard commands:

```powershell
cmake --preset default -DPIMIO_REQUIRE_LORE=ON
cmake --build --preset default
ctest --preset default     # Darkroom
ctest --preset studio      # Studio, on the sandbox desktop
cmake --install build\default --prefix stage
```

Flags:

| Flag | Effect |
|---|---|
| `-NoStudio` | Run Darkroom only. |
| `-NoLaunch` | Generate the `.wsb` file without starting the sandbox. |
| `-MemoryInMB <n>` | Override the sandbox memory (default 8192). |
| `-CacheRoot` / `-ResultsRoot` | Override the mapped host directories. |

The first run is slow, mostly because of the Build Tools installation; later
runs reuse the host cache but still reinstall inside the fresh sandbox.

The sandbox bootstrap now opens through a visible PowerShell window on the
sandbox desktop. If the sandbox window appears but the console does not, re-run
from the latest checkout so the generated `.wsb` uses the current logon command.

## What you get

Under `build\local-build\windows\<timestamp>\` on the host:

| File | Contents |
|---|---|
| `pimio-windows-x64.zip` | The staged application, only if Darkroom passed. |
| `darkroom-junit.xml`, `studio-junit.xml` | Test results. |
| `studio\` | Studio logs and screenshots. |
| `bootstrap.log`, `LastTest.log` | Full build and test output. |
| `environment.txt` | Commit, OS build, pinned tool versions, commands, and per-step status. |
| `pimio.wsb` | The exact sandbox configuration used. |

A failed Darkroom run means no staged package: the logs are still exported, and
`environment.txt` records why staging was skipped.

## Field Notes

When the automated steps finish, the sandbox stays open and the Field Notes
checklist ([../../../docs/plan/manual-testing.md](../../../docs/plan/manual-testing.md))
opens in Notepad. Run the manual pass against the staged application in
`C:\pimio\work\stage`, then close the sandbox window. Everything in
`C:\pimio\results` survives; the tools and working copy do not.

## Cleaning up

- Close the sandbox window to discard the guest.
- Delete a run's results directory under `build\local-build\windows\`.
- Delete `.cache\local-build\windows\` to drop the ~20 GB tool cache; the next
  `prepare.ps1` recreates it.

## Troubleshooting

**"Windows Sandbox is not available"** — the feature is not enabled, the
edition is Home, or virtualization is off in firmware. `prepare.ps1 -CheckOnly`
reports which one it is.

**A download fails checksum verification** — re-run with `-Force`. If it fails
again, the vendor changed the artifact behind a pinned URL; do not bypass the
check, update the pin in `pinned.ps1` instead.

**"The pinned versions do not match the repository"** — `pinned.ps1` cross-checks
its pins against `.github/workflows/ci.yml` and `cmake/PimioLore.cmake` so a
local build cannot drift from CI. Update `pinned.ps1` to match.

**Qt cannot be found in the sandbox** — the Qt download was incomplete. Re-run
`prepare.ps1` without `-SkipQt`.

**The sandbox has no network** — networking is enabled in the template, but
corporate policy can block it. Nothing in the bootstrap needs the network once
the cache is complete except the Build Tools installer, which downloads its
payload; run `prepare.ps1` on a machine that can reach Microsoft's servers.

**The sandbox opens but no bootstrap console appears** — Windows Sandbox can run
`<LogonCommand>` processes without showing their original console window. The
generated `.wsb` now launches the bootstrap through `cmd.exe start` so a visible
PowerShell window opens on the sandbox desktop; regenerate the sandbox from a
checkout that includes that change.

## Changing a pinned version

Edit `$PimioPinned` in `pinned.ps1`, keeping it consistent with
`.github/workflows/ci.yml` and `cmake/PimioLore.cmake`, then run
`prepare.ps1 -Force`. Pins exist so a local build reproduces CI; changing one
here without changing CI defeats the point.
