#!/bin/sh
# Runs the Studio suite (Tests B): automated GUI tests that need a real
# display, so they cannot run in the project's headless CI. Run this from a
# desktop session on Linux or macOS, then send the archive it produces back
# with any bug report.
#
# Usage: tools/field-tests/run-studio.sh
# Requires: a source checkout, CMake >= 3.24, Ninja, a C++20 compiler, Qt 6.
#
# Produces: pimio-studio-results-<timestamp>.tar.gz in the repository root,
# containing the test log, JUnit XML, per-test logs, and screenshots.

set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
cd "$repo"

stamp=$(date +%Y%m%d-%H%M%S)
results="$repo/build/studio-results/$stamp"
mkdir -p "$results"

export PIMIO_STUDIO_RESULTS="$results"

echo "== Studio tests: configure and build =="
cmake --preset default
cmake --build --preset default

echo "== Studio tests: run on the native display =="
status=0
ctest --preset studio --output-junit "$results/studio-junit.xml" \
    --output-log "$results/studio-ctest.log" || status=$?

{
    echo "pimio Studio (Tests B) run"
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(uname -a)"
    echo "exit status: $status"
    git -C "$repo" rev-parse HEAD 2>/dev/null | sed 's/^/commit: /' || true
} > "$results/environment.txt"

archive="$repo/pimio-studio-results-$stamp.tar.gz"
tar -C "$(dirname "$results")" -czf "$archive" "$(basename "$results")"

echo ""
echo "Results bundled into: $archive"
echo "Attach that file to a GitHub issue at https://github.com/dtmland/pimio/issues"
exit "$status"
