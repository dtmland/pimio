# Copilot instructions for pimio

## General

- Do not make claims without actually reading file contents - do not only look at file names and sizes, and do not speculate.
- Before deciding that a new dependency is needed that is not already in the project, please perform due diligence that any existing deps or tools cannot satisfy the need and document the justification.

## Repository health metrics

- Close to the end of each session, run `python3 tools/metrics/generate_repo_metrics.py` and review the generated reports under `docs/metrics/` to evaluate whether the work in that session should trigger refactoring of any files that have grown too large or have become too complex to maintain comfortably. Then when finished with any refactoring run the tool at the end again to capture the proper state of the repository.
- Treat this operation as standard operating procedure for repo health reporting. When a change affects the repository's structure or maintenance workflow, update the generated metrics reports so the repository history reflects the current state.
- Use the health snapshot as a guide for refactoring decisions. A practical default heuristic for this repository is: functions should generally stay at 20-50 LOC, classes at 200-300 LOC, files at 400-500 LOC, and line width should usually stay within 80-120 characters. Use these as signals for review, not hard rules: generated UI files, data models, configuration files, and complex algorithms may reasonably exceed them.
- Favor small, focused units of responsibility, and prefer to split a file or function when it becomes hard to understand at a glance, hard to test, or difficult to reason about without scrolling across multiple screens.

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

## Required completion gate for build-related changes

For any build-related change, Copilot **must not finalize** until the following
sections are present in the final response.

### 1) Cross-context impact matrix (required)

Include all rows below, each marked **Changed** or **Reviewed-no-change**, plus a
one-line reason:

- CI — `.github/workflows/ci.yml`
- Release — `.github/workflows/release.yml`
- Local Linux — `tools/local-build/linux/pinned.sh`, `tools/local-build/linux/Containerfile`
- Local Windows — `tools/local-build/windows/pinned.ps1`
- Shared build defs — `CMakePresets.json`, `CMakeLists.txt`, `cmake/PimioLore.cmake`
- Build architecture docs — `docs/build-architecture.md`

### 2) Placement review for new files/tools (required)

For any new script, helper, manual-test asset, or build/test tool:

- State whether it is **context-specific** or **cross-context**.
- If cross-context, place it in a shared location (for example `tools/` or
  `docs/`), not under a local-context directory.
- If placed in a context directory, include a one-line justification for why it
  is truly context-specific.

### 3) Pin/version drift check (required when pins change)

When changing a pinned version/module/checksum, explicitly confirm all relevant
pins were updated and drift asserts remain consistent across:

- `.github/workflows/ci.yml`
- `.github/workflows/release.yml`
- `cmake/PimioLore.cmake`
- local pinned scripts (`tools/local-build/linux/pinned.sh`, `tools/local-build/windows/pinned.ps1`)

### 4) Shared-first decision (required)

If logic is duplicated across workflows/scripts, either:

- move it to shared CMake/config/modules, or
- explain why duplication remains necessary.

Missing any required section above means the task is incomplete.
