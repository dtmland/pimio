# Repository Conventions

These conventions apply to all implementation increments described in
[pimio-v1-implementation.md](../pimio-v1-implementation.md).

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | UI-independent core library (`pimio::core`). Must not link Qt Gui, Qt Quick, or any UI module. |
| `src/core/include/pimio/core/` | Public core headers. Include as `#include "pimio/core/<name>.h"`. |
| `src/core/src/` | Core implementation files. |
| `src/lore/` | LORE-backed durable store (`pimio::lore`). Built only when LORE is available. `lore.h` stays private to this directory so the dependency has one boundary. |
| `src/projection/` | SQLite query cache (`pimio::projection`). The only place Qt Sql is linked. Holds nothing that cannot be rebuilt from the durable store. |
| `src/app/` | Qt application shell. `pimio::app_lib` holds testable startup logic; `pimio_app` is the thin executable. |
| `src/app/qml/` | QML files, exposed through the `Pimio` QML module. |
| `tests/core/` | Core unit and contract tests. No display, no network. |
| `tests/support/` | Test-only fakes (`pimio::test_support`): clock, filesystem, durable store, metadata reader, media-request service. |
| `tests/app/` | Application and QML smoke tests. |
| `tests/lore/` | LORE adapter, fault, and concurrency tests. Skip with a stated reason when LORE is absent. |
| `tests/projection/` | Schema migration and projection rebuild tests. |
| `tests/fixtures/` | Fixture manifest test. The media itself lives in `tests/fixtures/data/` with recorded provenance and hashes. See [tests/fixtures/README.md](../tests/fixtures/README.md). |
| `tools/` | Maintenance tools that are not shipped, such as the fixture generator. |
| `cmake/` | Reusable CMake modules, such as the pinned, checksum-verified LORE acquisition. |
| `docs/` | Policy and reference documents that outlive a single increment. |
| `docs/decisions/` | Numbered decision records. Written when a choice is made, not reconstructed afterwards. |
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

## Proving that CI reports failures

Configure with `-DPIMIO_ENABLE_FAILING_SELFTEST=ON`, or run the CI workflow
manually with the `failing_selftest` input enabled, to register
`selftest.deliberate_failure`. Every platform job must report a failure. This
is a negative control for CI observability and is off by default.

## Pull requests

Each pull request should identify its increment, list the CTest tests that
establish acceptance, and link the successful three-platform workflow run. A
skipped or allowed-to-fail platform is an incomplete increment.
