#!/usr/bin/env bash
# Open the pimio Field Notes manual-test checklist in a browser.
#
# Usage:
#   open-field-notes.sh [--build-dir <dir>]
#
# Options:
#   --build-dir <dir>   Path to the staged build output (for reference in the
#                       console message).  Defaults to the value of the
#                       PIMIO_STAGE_DIR environment variable, or empty.
#
# The script opens tools/manual-test/field-notes.html relative to the
# repository root (auto-detected from the script's own location).
# If no browser opener is available (headless / SSH) it prints the file URL
# instead of failing.

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
html_file="$script_dir/field-notes.html"

build_dir="${PIMIO_STAGE_DIR:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            build_dir="$2"
            shift 2
            ;;
        *)
            echo "Usage: $0 [--build-dir <dir>]" >&2
            exit 1
            ;;
    esac
done

if [[ ! -f "$html_file" ]]; then
    echo "Field Notes not found: $html_file" >&2
    exit 1
fi

file_url="file://$html_file"

# Try openers in preference order.
opened=false
for opener in xdg-open x-www-browser gnome-open open; do
    if command -v "$opener" &>/dev/null; then
        "$opener" "$file_url" &
        opened=true
        break
    fi
done

if $opened; then
    echo "Field Notes opened in the browser: $file_url"
else
    echo "No browser opener found (headless environment?)."
    echo "Open manually: $file_url"
fi

if [[ -n "$build_dir" ]]; then
    echo "Staged application: $build_dir"
fi
