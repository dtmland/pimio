# Repository Conventions

These conventions apply to all implementation increments described in
[pimio-v1-implementation.md](../pimio-v1-implementation.md).

## Layout

| Path | Contents |
| --- | --- |
| `src/core/` | UI-independent core library (`pimio::core`). Must not link Qt Gui, Qt Quick, or any UI module. |
| `src/core/include/pimio/core/` | Public core headers. Include as `#include "pimio/core/<name>.h"`. |
| `src/core/src/` | Core implementation files. |
| `src/app/` | Qt application shell. `pimio::app_lib` holds testable startup logic; `pimio_app` is the thin executable. |
| `src/app/qml/` | QML files, exposed through the `Pimio` QML module. |
| `tests/core/` | Core unit and contract tests. No display, no network. |
| `tests/app/` | Application and QML smoke tests. |
| `tests/fixtures/` | Small owned or explicitly licensed test media, with recorded provenance and hashes. |
| `docs/` | Policy and reference documents that outlive a single increment. |
| `build/` | Generated output. Never committed. |

Large media corpora are managed outside the repository and are used only for
performance and soak tests.

## Naming

- Namespaces: `pimio::core`, `pimio::app`.
- CMake targets: `pimio_<component>` with a `pimio::<component>` alias.
- Test executables: `tst_<area>_<subject>`.
- CTest names: `<area>.<subject>`, for example `core.version` and `app.smoke`.
  The same CTest names are used locally and in CI.

## Code style

- C++20, four-space indentation, no tabs.
- `#pragma once` in headers.
- One class per header where practical.
- Prefer `QStringLiteral` for compile-time Qt strings.
- Keep core types free of UI and framework assumptions so the 2.0.0 rendering
  frontend can reuse them.

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
