# Testing tiers: Darkroom, Studio, and Field Notes

pimio's tests are organized into three named tiers by *who can run them and
where*. Every check starts life in the most automated tier it can possibly
live in; a check only moves down a tier when there is a concrete technical
reason it cannot be automated further.

| Tier | Name | What it is | Who runs it |
| --- | --- | --- | --- |
| Tests A | **Darkroom** | Fully automated, headless. Runs unattended without a display. | CI on every push and release, and anyone from a source checkout. |
| Tests B | **Studio** | Fully automated GUI tests that need a real screen. | You, on your desktop machines; CI approximates them under Xvfb on Linux only. |
| Tests C | **Field Notes** | Manual checks that resist automation (visual judgement, external hardware, OS dialogs). | You, following written steps, reporting with the template below. |

## Tests A — Darkroom

The Darkroom suite is the CTest suite run with the offscreen Qt platform: unit
tests, contract tests, fault-injection tests, projection/migration tests, and
the application smoke test. CI runs it on Linux, Windows, and macOS on every
push (`.github/workflows/ci.yml`), and every release additionally extracts
each published archive on a clean machine and runs `pimio-doctor` plus
`pimio --self-check` against it (`.github/workflows/release.yml`) — that
release step is a packaging verification, not a separate test suite.

Run it yourself from a source checkout:

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Requirements: CMake >= 3.24, Ninja, a C++20 compiler, Qt 6 (>= 6.4). No
display is needed; the preset forces `QT_QPA_PLATFORM=offscreen`. On a release
archive (no source build), the Darkroom equivalent is:

```
QT_QPA_PLATFORM=offscreen ./pimio --self-check   # pimio.bat --self-check on Windows
./pimio-doctor                                    # pimio-doctor.ps1 on Windows
```

### Opt-in LORE server promotion gate

`lore.server_promotion` is retained as an automated client/server contract
gate, but is not part of the standard Darkroom run while its LORE 0.9.0 expected
failures remain. It acquires the checksum-verified `loreserver`, launches an
unauthenticated loopback-only server with isolated configuration and storage,
and removes the topology after the run:

```
cmake --preset default -DPIMIO_ENABLE_LORE_SERVER_TESTS=ON
cmake --build --preset default --target tst_lore_server_promotion
ctest --test-dir build/default -R '^lore.server_promotion$' -V
```

Five consecutive Linux measurements took 3.657–3.916 seconds of CTest wall
time (median 3.778 seconds), so runtime would not prevent adding it to CI. Keep
it opt-in until the expected failures documented under Increment 7.8b are
resolved; an upstream fix produces an unexpected pass and forces the evidence
to be reviewed.

## Tests B — Studio

Studio tests live in `tests/studio/`. They are ordinary automated CTest
executables, but they open the real application window on the real desktop,
drive it programmatically, assert on what is on screen, and save screenshots.
CI has no display on Windows and macOS, so those runs are yours; on Linux, CI
runs the same tests under Xvfb (`ctest --preset default-x11`), which is close
to but not the same as a real desktop session.

Run them with the bundling script, from a desktop session (not SSH):

- Linux / macOS: `tools/field-tests/run-studio.sh`
- Windows (from a Developer PowerShell for VS prompt):
  `powershell -ExecutionPolicy Bypass -File tools\field-tests\run-studio.ps1`

The script configures, builds, runs every test labeled `studio`, and bundles
everything into a single archive in the repository root:

```
pimio-studio-results-<timestamp>.tar.gz   (.zip on Windows)
  environment.txt            OS, host, commit, exit status
  studio-ctest.log           full CTest output
  studio-junit.xml           machine-readable results
  *.log                      per-test logs
  *.png                      screenshots taken during the run
```

**Reporting:** attach the whole archive to a GitHub issue at
<https://github.com/dtmland/pimio/issues>, one issue per failing run, titled
`Studio run failure: <platform> <version>`. The archive is designed to be
self-contained — no additional description is needed, though observations are
welcome.

To run the suite without the script: `ctest --preset studio`. Set
`PIMIO_STUDIO_RESULTS=<dir>` to choose where logs and screenshots are written.

On Windows you can also get a Studio run without installing a toolchain at all,
using the disposable sandbox described in
[tools/local-build/windows/README.md](../tools/local-build/windows/README.md);
it runs Darkroom and Studio and exports the same kind of results bundle.

## Tests C — Field Notes

Field Notes are the checks that stay manual as a last resort: judging visual
quality, real window-manager and multi-monitor behavior, OS permission
dialogs, drag-and-drop from other applications, and anything involving
external devices. The step-by-step entries live in
[docs/plan/manual-testing.md](plan/manual-testing.md); each states its
conditions, steps, and acceptance criteria.

**Reporting:** file one GitHub issue per test entry using this template:

```
Test: MT-<n> — <title>
Platform: <OS and version, display server, hardware>
pimio version: <from the title bar or `pimio --version`>
Result: PASS / FAIL
Observed: <what actually happened, step by step where it diverged>
Expected: <the acceptance criterion that was not met>
Attachments: screenshots, pimio-doctor-report.txt if the app misbehaved
```

A PASS is worth reporting too — the manual plan requires a sign-off with
platform, OS version, Qt version, hardware, and result for each entry.

## Adding a new test

Choose the tier top-down:

1. Can it assert its result without a display? → Darkroom (`tests/<area>/`,
   no label).
2. Does it need a screen but can a program still drive it and check the
   outcome? → Studio (`tests/studio/`, CTest label `studio`).
3. Only if a human judgement or an environment we cannot script is essential →
   Field Notes (a new MT entry in `docs/plan/manual-testing.md`).
