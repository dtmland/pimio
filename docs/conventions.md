# Repository Conventions

These conventions apply to all implementation increments described in
[pimio-v1-implementation.md](plan/pimio-v1-implementation.md).

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | UI-independent core library (`pimio::core`). Must not link Qt Gui, Qt Quick, or any UI module. |
| `src/core/include/pimio/core/` | Public core headers. Include as `#include "pimio/core/<name>.h"`. |
| `src/core/src/` | Core implementation files. |
| `src/lore/` | LORE-backed durable store (`pimio::lore`). Built only when LORE is available. `lore.h` stays private to this directory so the dependency has one boundary. |
| `src/metadata/` | Metadata read adapters (`pimio::metadata`). Container parsers are private to `src/metadata/src/`; the only public surface is the `core::MetadataReader` implementation. |
| `src/scan/` | Incremental scan and media identity (`pimio::scan`). Reaches the filesystem only through `core::FileSystem`. |
| `src/projection/` | SQLite query cache (`pimio::projection`). The only place Qt Sql is linked. Holds nothing that cannot be rebuilt from the durable store. |
| `src/settings/` | User settings (`pimio::settings`). Links only Qt Core so any component can read settings without a cycle. Stored settings persist to `pimio.conf`; session settings do not. See [decisions/0003-settings-and-view-controls.md](decisions/0003-settings-and-view-controls.md). |
| `src/app/` | Qt application shell. `pimio::app_lib` holds testable startup logic; `pimio_app` is the thin executable. |
| `src/app/qml/` | QML files, exposed through the `Pimio` QML module. |
| `tests/core/` | Core unit and contract tests. No display, no network. |
| `tests/support/` | Test-only fakes (`pimio::test_support`): clock, filesystem, durable store, metadata reader, media-request service. |
| `tests/app/` | Application and QML smoke tests. |
| `tests/lore/` | LORE adapter, fault, and concurrency tests. Skip with a stated reason when LORE is absent. |
| `tests/projection/` | Schema migration, projection rebuild, and sorting tests. |
| `tests/settings/` | Settings defaults, clamping, and persistence tests. |
| `tests/fixtures/` | Fixture manifest test. The media itself lives in `tests/fixtures/data/` with recorded provenance and hashes. See [tests/fixtures/README.md](../tests/fixtures/README.md). |
| `tests/studio/` | Studio (Tests B) GUI tests: automated, but they need a real display. CTest label `studio`; excluded from the offscreen `default` preset, run by the `studio` preset and under Xvfb by `default-x11`. See [docs/testing.md](testing.md). |
| `tools/` | Maintenance tools that are not shipped, such as the fixture generator. |
| `tools/field-tests/` | `run-studio.sh` / `run-studio.ps1`: run the Studio suite on a desktop machine and bundle logs and screenshots for reporting. |
| `tools/local-build/` | Per-platform reproducible local build environments: pinned definitions, launchers, and bootstrap scripts. Generated images, vendor SDKs, and build output stay out of Git. See the [Linux](../tools/local-build/linux/README.md) and [Windows](../tools/local-build/windows/README.md) instructions, and [docs/build-architecture.md](build-architecture.md) for how these relate to CI and Release. |
| `packaging/` | Files shipped at the root of each release archive: the launcher, the per-platform `README.txt`, and the `pimio-doctor` diagnostic script. Installed by `cmake --install`, so a local install matches a release. |
| `cmake/` | Reusable CMake modules, such as the pinned, checksum-verified LORE acquisition. |
| `docs/` | All project documentation. The repository root holds only files a project normally keeps there, such as `README.md` and build configuration. |
| `docs/decisions/` | Numbered decision records. Written when a choice is made, not reconstructed afterwards. |
| `docs/plan/` | Product vision, release plans, and the implementation plan, plus `progress.md`, the record of which increments are complete. |
| `.cache/` | Verified third-party downloads, keyed by version. Never committed; safe to delete. |
| `build/` | Generated output. Never committed. |

Large media corpora are managed outside the repository and are used only for
performance and soak tests.

## Naming

- Namespaces: `pimio::core`, `pimio::app`.
- CMake targets: `pimio_<component>` with a `pimio::<component>` alias.
- Test executables: `tst_<area>_<subject>`.
- CTest names: `<area>.<subject>`, for example `core.version`, `core.contracts`,
  `fixtures.manifest`, and `app.smoke`. The same CTest names are used locally
  and in CI.

## Code style

- C++20, four-space indentation, no tabs.
- `#pragma once` in headers.
- One class per header where practical.
- Prefer `QStringLiteral` for compile-time Qt strings.
- Keep core types free of UI and framework assumptions so the 2.0.0 rendering
  frontend can reuse them. `pimio::core` links only `Qt6::Core`.
- Serialized records carry `schemaVersion` and preserve unrecognized fields, so
  a record written by a newer release survives a read/modify/write cycle by an
  older one. New record types must follow the same pattern.
- Nothing in the core reads the clock or the filesystem directly. Use the
  `Clock` and `FileSystem` boundaries so behavior stays testable.

## Build and test commands

The same preset names are used locally and in CI:

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

On Linux, the X11 layer additionally runs:

```
xvfb-run -a --server-args="-screen 0 1280x1024x24" ctest --preset default-x11
```

The `default` test preset sets `QT_QPA_PLATFORM=offscreen`; `default-x11` sets
`QT_QPA_PLATFORM=xcb`.

Every test preset also sets `QT_ASSUME_STDERR_HAS_CONSOLE=1`. Without it, a
Windows process with no console window (which is how CI and most launchers run
the tests) sends all Qt Test output to `OutputDebugString` instead of stdout, so
CTest records an empty log and a failing test reports nothing but its name.

## Proving that CI reports failures

Configure with `-DPIMIO_ENABLE_FAILING_SELFTEST=ON`, or run the CI workflow
manually with the `failing_selftest` input enabled, to register
`selftest.deliberate_failure`. Every platform job must report a failure. This
is a negative control for CI observability and is off by default.

## Pull requests

Each pull request should identify its increment, list the CTest tests that
establish acceptance, and link the successful three-platform workflow run. A
skipped or allowed-to-fail platform is an incomplete increment.
