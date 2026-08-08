# Copilot instructions for pimio

## Build changes must be propagated across every context

pimio is built in four contexts that share build commands but provision their
environments separately. The build commands live in one place; the *environment*
is duplicated. When a prompt touches **any** part of the build — a dependency, a
pinned version, a Qt module, a compiler flag, a system package, a build/test/
deploy step — treat it as a change to all relevant contexts, not just the one in
front of you. Missing one is how "works in CI, breaks locally" bugs happen (for
example, adding a `Qt6::Multimedia` link without adding the `qtmultimedia`
module everywhere).

Before finishing a build-related change, check whether it also needs to be made
in:

- **CI** — `.github/workflows/ci.yml` (build + test, all platforms).
- **Release** — `.github/workflows/release.yml` (build + deploy + archive).
- **Local Linux** — `tools/local-build/linux/` (`pinned.sh`, `Containerfile`).
- **Local Windows** — `tools/local-build/windows/` (`pinned.ps1`).
- **Shared build definition** — `CMakePresets.json`, `CMakeLists.txt`,
  `cmake/PimioLore.cmake`.

### Rules of thumb

- **Pinned versions must match everywhere** (Qt version, Qt modules, LORE
  version/checksums). `ci.yml` and `cmake/PimioLore.cmake` are authoritative; the
  local `pinned.*` files re-read them and the drift-asserts also require
  `release.yml` to agree. If you change a pin, update every file and confirm the
  asserts still pass.
- **Prefer the shared source.** If behavior can live in `CMakePresets.json` or a
  `cmake/` module instead of being duplicated in each workflow/script, put it
  there.
- **System package lists are intentionally per-context** (CI needs `xvfb`,
  Release needs `patchelf`/Wayland, the container is the superset). When adding a
  system dependency, decide which contexts actually need it rather than copying
  it blindly — and keep the reasoning consistent with the table in
  `docs/build-architecture.md`.
- **Consult and update `docs/build-architecture.md`** — it records what is shared
  vs. per-context and why. Keep it current when the build layout changes.
