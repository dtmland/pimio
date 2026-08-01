#!/bin/sh
# pimio-doctor -- collects one pasteable report about a pimio release archive.
#
# Run it from anywhere; it inspects the tree it is shipped in, never the system
# installation. It writes pimio-doctor-report.txt next to itself (or to the
# current directory when the archive is on read-only media) and prints the same
# text to stdout.
#
# It uses only POSIX shell and tools present in a default desktop install. It
# prints no environment variables other than the display-related ones it names
# explicitly, so a report is safe to paste into a public issue.
#
# Exit status: 0 when no hard problem was found, 1 otherwise.

set -u

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
os=$(uname -s 2>/dev/null || echo unknown)

report="$here/pimio-doctor-report.txt"
if ! : >"$report" 2>/dev/null; then
    report="./pimio-doctor-report.txt"
    if ! : >"$report" 2>/dev/null; then
        echo "pimio-doctor: cannot write a report file; printing to stdout only" >&2
        report=/dev/null
    fi
fi

problems=""

note_problem() {
    problems="${problems}  - $1
"
}

say() {
    printf '%s\n' "$*" | tee -a "$report"
}

section() {
    say ""
    say "== $1 =="
}

run() {
    # Runs a command if it exists, reporting its absence rather than failing.
    if command -v "$1" >/dev/null 2>&1; then
        "$@" 2>&1 | sed 's/^/  /' | tee -a "$report"
    else
        say "  ($1 not available)"
    fi
}

if [ "$os" = "Darwin" ]; then
    app_binary="$here/pimio.app/Contents/MacOS/pimio"
    [ -x "$app_binary" ] || app_binary="$here/bin/pimio"
    lib_suffix=".dylib"
    platform_plugin_dir="$here/pimio.app/Contents/PlugIns/platforms"
    [ -d "$platform_plugin_dir" ] || platform_plugin_dir="$here/plugins/platforms"
else
    app_binary="$here/bin/pimio"
    lib_suffix=".so"
    platform_plugin_dir="$here/plugins/platforms"
fi

say "pimio-doctor report"
say "generated: $(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo unknown)"
say "archive root: $here"

section "System"
say "  uname: $(uname -a 2>/dev/null || echo unknown)"
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    say "  distribution: $(. /etc/os-release && printf '%s' "${PRETTY_NAME:-unknown}")"
fi
if [ "$os" = "Darwin" ]; then
    say "  macOS: $(sw_vers -productVersion 2>/dev/null || echo unknown)"
    say "  architecture: $(uname -m 2>/dev/null || echo unknown)"
else
    if command -v ldd >/dev/null 2>&1; then
        say "  glibc: $(ldd --version 2>/dev/null | head -n 1)"
    fi
fi

section "Display session"
say "  XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<unset>}"
say "  WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}"
say "  DISPLAY=${DISPLAY:-<unset>}"
say "  QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-<unset>}"
if [ "$os" != "Darwin" ] && [ -z "${QT_QPA_PLATFORM:-}" ] &&
    [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    note_problem "No display server is reachable: both DISPLAY and WAYLAND_DISPLAY are unset. Run inside a desktop session, or set QT_QPA_PLATFORM=offscreen to test headlessly."
fi

section "Archive layout"
layout_ok=1
check_path() {
    if [ -e "$1" ]; then
        say "  ok      $2"
    else
        say "  MISSING $2"
        layout_ok=0
    fi
}
check_path "$app_binary" "application binary ($app_binary)"
if [ "$os" = "Darwin" ]; then
    check_path "$here/pimio.app/Contents/Info.plist" "bundle Info.plist"
else
    check_path "$here/bin/qt.conf" "bin/qt.conf"
    check_path "$here/lib" "lib/"
fi
check_path "$platform_plugin_dir" "platform plugin directory"
if [ "$os" = "Darwin" ]; then
    check_path "$here/pimio.app/Contents/Resources/qml" "QML import tree"
else
    check_path "$here/qml" "qml/"
    check_path "$here/qml/QtQuick" "qml/QtQuick"
fi
if [ "$layout_ok" -eq 0 ]; then
    note_problem "The archive layout is incomplete. Re-extract the archive without moving or renaming anything inside it, and do not copy individual files out of the tree."
fi

section "qt.conf"
qt_conf="$here/bin/qt.conf"
[ -f "$qt_conf" ] || qt_conf="$here/pimio.app/Contents/Resources/qt.conf"
if [ -f "$qt_conf" ]; then
    say "  file: $qt_conf"
    sed 's/^/    /' "$qt_conf" | tee -a "$report"
    say "  resolved (relative to $(dirname -- "$qt_conf")):"
    prefix=$(sed -n 's/^[Pp]refix[[:space:]]*=[[:space:]]*//p' "$qt_conf" | head -n 1)
    if [ -n "$prefix" ]; then
        resolved=$(CDPATH= cd -- "$(dirname -- "$qt_conf")" 2>/dev/null &&
            cd -- "$prefix" 2>/dev/null && pwd -P)
        if [ -n "$resolved" ]; then
            say "    Prefix -> $resolved"
            if [ "$resolved" != "$here" ]; then
                note_problem "qt.conf Prefix resolves to $resolved, which is not the archive root $here. The archive was repacked with the wrong directory layout; Qt will look for plugins outside the extracted tree."
            fi
        else
            say "    Prefix -> <does not exist: $prefix>"
            note_problem "qt.conf Prefix points at a directory that does not exist."
        fi
    else
        say "    <no Prefix entry>"
    fi
else
    say "  (no qt.conf found)"
fi

section "Platform plugins present"
if [ -d "$platform_plugin_dir" ]; then
    ls "$platform_plugin_dir" | sed 's/^/  /' | tee -a "$report"
    # A QT_QPA_PLATFORM naming a plugin that is not in the archive aborts the
    # application with "no Qt platform plugin could be initialized", which says
    # nothing about which name was requested.
    requested=$(printf '%s' "${QT_QPA_PLATFORM:-}" | sed -n 's/^\([a-z0-9]*\)\(:.*\)\{0,1\}$/\1/p')
    if [ -n "$requested" ] &&
        ! ls "$platform_plugin_dir"/libq"$requested"*"$lib_suffix" >/dev/null 2>&1; then
        note_problem "QT_QPA_PLATFORM requests the '$requested' platform plugin, but libq$requested$lib_suffix is not in $platform_plugin_dir. Unset QT_QPA_PLATFORM, or choose one of the plugins listed above."
    fi
else
    say "  (none: $platform_plugin_dir does not exist)"
fi

section "Library dependencies"
missing_libs=""
scan_one() {
    say "  $1"
    if [ "$os" = "Darwin" ]; then
        otool -L "$1" 2>&1 | tail -n +2 | sed 's/^/    /' | tee -a "$report"
    else
        out=$(ldd "$1" 2>&1)
        printf '%s\n' "$out" | sed 's/^/    /' | tee -a "$report"
        unresolved=$(printf '%s\n' "$out" | grep 'not found' | awk '{ print $1 }')
        if [ -n "$unresolved" ]; then
            missing_libs="$missing_libs
$unresolved"
        fi
    fi
}

if [ -x "$app_binary" ]; then
    scan_one "$app_binary"
else
    say "  (application binary not found; skipping)"
fi
lib_list=$(mktemp 2>/dev/null) || lib_list=""
if [ -n "$lib_list" ]; then
    find "$here" -type f -name "*$lib_suffix*" 2>/dev/null | sort >"$lib_list"
    while IFS= read -r lib; do
        [ -n "$lib" ] && scan_one "$lib"
    done <"$lib_list"
    rm -f "$lib_list"
else
    # No mktemp: fall back to word splitting rather than to a predictable
    # path in /tmp, which a symlink planted ahead of us could hijack.
    for lib in $(find "$here" -type f -name "*$lib_suffix*" 2>/dev/null | sort); do
        scan_one "$lib"
    done
fi
if [ -n "$missing_libs" ]; then
    unique_missing=$(printf '%s\n' "$missing_libs" | grep -v '^$' | sort -u | tr '\n' ' ')
    section "Unresolved libraries"
    say "  $unique_missing"
    note_problem "These shared libraries could not be resolved: $unique_missing. Install the matching system packages (on Debian and Ubuntu, libxcb-cursor0 provides libxcb-cursor.so.0)."
fi

section "Graphics"
run glxinfo -B
run eglinfo -B

section "Application launch (QT_DEBUG_PLUGINS=1)"
launch_status=skipped
if [ -x "$app_binary" ]; then
    launcher=""
    if command -v timeout >/dev/null 2>&1; then
        launcher="timeout 60"
    fi
    launch_output=$(cd -- "$here" && QT_DEBUG_PLUGINS=1 \
        QT_LOGGING_RULES='qt.qpa.*=true' \
        $launcher "$app_binary" --version 2>&1)
    launch_status=$?
    printf '%s\n' "$launch_output" | sed 's/^/  /' | tee -a "$report"
    say "  exit status: $launch_status"
    if [ "$launch_status" -ne 0 ]; then
        case "$launch_output" in
        *"no Qt platform plugin could be initialized"*)
            note_problem "Qt could not initialize a platform plugin. The plugin search path and any dlopen error appear in the launch output above."
            ;;
        *"is not installed"*)
            note_problem "A QML module is missing from this build. The archive was produced without the QML import tree."
            ;;
        *)
            note_problem "The application exited with status $launch_status. See the launch output above."
            ;;
        esac
    fi
else
    say "  (application binary not found; skipping)"
fi

if [ "$os" = "Darwin" ] && [ -e "$here/pimio.app" ]; then
    section "macOS quarantine and signature"
    run xattr -l "$here/pimio.app"
    run codesign --verify --deep --verbose=2 "$here/pimio.app"
    if command -v codesign >/dev/null 2>&1 &&
        ! codesign --verify --deep --strict "$here/pimio.app" >/dev/null 2>&1; then
        note_problem "The bundle's code signature does not verify. macOS kills such a process at startup (crash report: \"Termination Reason: CODESIGNING, Invalid Page\"). Clearing quarantine does not help; the download itself is damaged or was built unsigned -- please report this with the output above."
    fi
    if xattr -p com.apple.quarantine "$here/pimio.app" >/dev/null 2>&1; then
        note_problem "The bundle is quarantined by Gatekeeper. Clear it with: xattr -dr com.apple.quarantine \"$here/pimio.app\""
    fi
fi

section "LIKELY CAUSE"
if [ -z "$problems" ]; then
    say "  No hard problem detected. The archive looks complete and the"
    say "  application started successfully."
    exit_code=0
else
    printf '%s' "$problems" | tee -a "$report"
    exit_code=1
fi

say ""
say "Report written to: $report"
exit "$exit_code"
