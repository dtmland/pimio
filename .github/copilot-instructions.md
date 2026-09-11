# Copilot instructions for pimio

## General

- Do not make claims without actually reading file contents - do not only look at file names and sizes, and do not speculate.
- Before deciding that a new dependency is needed that is not already in the project, please perform due diligence that any existing deps or tools cannot satisfy the need and document the justification.

## Crash-resistant progress

- For implementation tasks, commit and push the current branch with
  `report_progress` after each coherent unit of work and whenever material
  changes have remained unpushed for 20 minutes. Also checkpoint immediately
  before long-running validation or risky operations and after accepting
  substantial work from a subagent.
- A local commit is not a durable checkpoint in the cloud-agent environment.
  Push the checkpoint branch even when the work is incomplete, and identify
  unfinished or unvalidated work in the progress checklist. Prefer a focused
  checkpoint commit over risking the loss of substantial work.
- Run `git diff --check` and exclude temporary or generated files before a
  checkpoint. Run focused tests when practical, but do not defer preservation
  of substantial work solely because full validation is still pending.
- Do not create a pull request until implementation and validation are complete
  and the work is ready for final CI and review. Until then, push checkpoint
  commits only to the feature branch so the `pull_request` workflow is not
  triggered on every checkpoint. Create an earlier draft PR only when the
  maintainer explicitly requests one.

## Metadata write direction

- Prefer standards-compatible embedded metadata updates in managed originals.
  Do not introduce or default to metadata sidecar files unless their necessity
  is documented and the maintainer gives explicit approval. Follow
  [decision 0007](../docs/decisions/0007-embedded-metadata-writes.md) when
  implementing metadata writes.

## Repository health metrics

- Close to the end of each session, run `python3 tools/metrics/generate_repo_metrics.py` and review the generated reports under `docs/metrics/` to evaluate whether the work in that session should trigger refactoring of any files that have grown too large or have become too complex to maintain comfortably. Then when finished with any refactoring run the tool at the end again to capture the proper state of the repository.
- Treat this operation as standard operating procedure for repo health reporting. When a change affects the repository's structure or maintenance workflow, update the generated metrics reports so the repository history reflects the current state.
- Use the health snapshot as a guide for refactoring decisions. A practical default heuristic for this repository is: functions should generally stay at 20-50 LOC, classes at 200-300 LOC, files at 400-500 LOC, and line width should usually stay within 80-120 characters. Use these as signals for review, not hard rules: generated UI files, data models, configuration files, and complex algorithms may reasonably exceed them.
- Favor small, focused units of responsibility, and prefer to split a file or function when it becomes hard to understand at a glance, hard to test, or difficult to reason about without scrolling across multiple screens.

## Build changes must be propagated across every context

pimio is built in four contexts that share build commands but provision their
environments separately. Product pins and common Linux packages are shared;
context-specific provisioning still differs. When a prompt touches **any** part of the build — a dependency, a
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

- **Change authoritative inputs, not copies.** Qt version/modules and the local
  aqtinstall version belong only in `tools/build/qt.env`. LORE version, base URL,
  and checksums belong only in `cmake/PimioLore.cmake`. The shared bootstrap
  readers in `tools/build/pins.sh` and `pins.ps1` feed CI, Release, and the local
  harnesses. Do not add product pin literals to workflows, local `pinned.*`
  files, or `Containerfile` defaults. Update the source once and verify consumers.
- **Prefer the shared source.** If behavior can live in `CMakePresets.json` or a
  `cmake/` module instead of being duplicated in each workflow/script, put it
  there.
- **Common prerequisites must be shared.** All Linux build contexts consume
  `tools/build/linux-packages.txt` (including codec tools such as NASM and Perl).
  Keep only context-specific extras in workflows/the container: CI needs `xvfb`,
  Release needs `patchelf`/Wayland, and the container needs both plus its toolchain.
  Trace transitive dependencies too: a FetchContent codec can require an assembler,
  Perl, or Git even when pimio's own CMake does not directly mention that tool.
  Never assume a tool on a hosted runner exists in a clean container or Sandbox.
- **Consult and update `docs/build-architecture.md`** — it records what is shared
  vs. per-context and why. Keep it current when the build layout changes.

### Executable evidence, not just a checklist

- Run `python -m unittest discover -s tests/build -v` for every build change.
  These offline contracts run before provisioning in CI and Release; they
  exercise the real Bash/PowerShell readers and compare Windows checksums with
  CMake evaluation. When changing a reader's input format, update both readers
  and add a mutation test proving propagation or fail-closed behavior.
- The Local Linux container is temporarily excluded from CI because its
  from-scratch image build exceeds the useful feedback window. Run the actual
  `tools/local-build/linux/build.sh` entry point manually for relevant changes.
  Native hosted-runner jobs alone do not validate local provisioning. Rebuild
  the image after package changes; an old `--use-image`/`--pull` image is not
  evidence for the committed Containerfile.
- Windows bootstrap tests must run with Windows PowerShell 5.1, not only `pwsh`.
  For changes to portable downloads, compiler setup, or Sandbox orchestration,
  also obtain a fresh Windows Sandbox run when available. Reader tests are not
  an end-to-end Sandbox test; report unavailable validation explicitly.
- A prose matrix is not an enforcement mechanism. Any newly unavoidable
  duplication needs an automated contract and a documented reason. Do not
  suppress a failing checksum check or disable codec optimizations to hide drift.

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
