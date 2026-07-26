#!/bin/sh
# pimio launcher.
#
# Runs the application no matter what the current working directory is. The Qt
# deployment resolves its plugins through bin/qt.conf, whose Prefix is relative
# to the executable, so the whole extracted tree must stay together and the
# executable must be started from its own location.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)

if [ -x "$here/pimio.app/Contents/MacOS/pimio" ]; then
    exec "$here/pimio.app/Contents/MacOS/pimio" "$@"
fi

app="$here/bin/pimio"
if [ ! -x "$app" ]; then
    echo "pimio: cannot find the application binary at $app" >&2
    echo "pimio: extract the whole archive and keep its directory layout intact." >&2
    exit 1
fi

# Prefer the session's own windowing system, but fall back to X11 (through
# XWayland) when this build carries no Wayland platform plugin. Anything the
# user set explicitly wins.
if [ -z "${QT_QPA_PLATFORM:-}" ] && [ "$(uname -s)" != "Darwin" ]; then
    if [ "${XDG_SESSION_TYPE:-}" = "wayland" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        if [ -e "$here/plugins/platforms/libqwayland-generic.so" ] ||
            [ -e "$here/plugins/platforms/libqwayland-egl.so" ]; then
            QT_QPA_PLATFORM=wayland
        else
            QT_QPA_PLATFORM=xcb
        fi
        export QT_QPA_PLATFORM
    fi
fi

cd -- "$here"
exec "$app" "$@"
