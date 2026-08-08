#!/usr/bin/env bash
# Open the pimio Field Notes manual-test checklist in a Linux browser.
#
# Usage:
#   tools/local-build/linux/open-field-notes.sh [--build-dir <dir>]
#
# Options:
#   --build-dir <dir>   Path to the staged build output (for the console
#                       message).  Defaults to the value of the
#                       PIMIO_STAGE_DIR environment variable, or empty.
#
# Delegates to tools/manual-test/open-field-notes.sh, which contains the
# shared opener logic (xdg-open with graceful fallback for headless hosts).

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
shared_launcher="$script_dir/../../manual-test/open-field-notes.sh"

exec "$shared_launcher" "$@"
