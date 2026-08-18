pimio for Linux
===============

What is in this archive
-----------------------

  pimio            launcher script -- start the application with this
  pimio-doctor     diagnostic script -- run this when something goes wrong
  bin/             the application binary, qt.conf, and liblore.so
  lib/             bundled Qt and X11 libraries
  plugins/         Qt plugins, including plugins/platforms
  qml/             QML modules the application imports
  translations/    Qt's own translations

How to run it
-------------

  1. Extract the archive anywhere you can write, for example
     tar -xzf pimio-<tag>-Linux-binaries.tar.gz -C ~/pimio
  2. Run the launcher:
     ~/pimio/pimio

The launcher works from any working directory. You can also run bin/pimio
directly, but only from the extracted directory itself: Qt finds its plugins
through bin/qt.conf, whose Prefix is relative to the executable's location.

Keep the whole tree together. Do not move bin/pimio, the plugins directory, or
individual libraries somewhere else; the layout is what makes the archive
self-contained. If you want pimio on your PATH, add a symbolic link to the
`pimio` launcher rather than to bin/pimio.

System packages you still need
------------------------------

The archive bundles Qt, but a few libraries must come from your distribution:

  Debian, Ubuntu:
    sudo apt install libxcb-cursor0 libgl1 libegl1 libxkbcommon-x11-0 libpulse0

  Fedora:
    sudo dnf install xcb-util-cursor mesa-libGL mesa-libEGL libxkbcommon-x11 pulseaudio-libs

libxcb-cursor0 is required by Qt 6.5 and later. Without it the xcb platform
plugin is found but cannot be loaded, and startup fails with
"Could not load the Qt platform plugin \"xcb\" in \"\" even though it was found."

libpulse0 (PulseAudio) is required by Qt Multimedia for audio playback. Most
desktop installations already include it; headless servers may not.

Wayland and X11
---------------

On a Wayland session the launcher uses the Wayland platform plugin when this
build ships one, and otherwise falls back to X11 through XWayland. To choose
explicitly:

  QT_QPA_PLATFORM=xcb ./pimio
  QT_QPA_PLATFORM=wayland ./pimio

Headless machines
-----------------

The archive also ships the offscreen platform plugin, so the build can be
started without any display server, for example to verify an installation over
SSH:

  QT_QPA_PLATFORM=offscreen ./pimio --self-check

Resetting local rebuildable state
---------------------------------

Before deleting local state, close pimio.

pimio stores user-level derived data on this machine, including a rebuildable
SQLite projection/index and thumbnail/preview caches. Deleting it is safe: it
does not remove the library's durable data, and pimio will recreate it. This
is useful when you want a clean local rebuild between version upgrades.

  rm -rf "${XDG_STATE_HOME:-$HOME/.local/state}/pimio"
  rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/pimio"
  rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/pimio"

If you use non-default XDG environment variables, those change the effective
paths. After cleanup, start pimio again and allow it to rebuild its
caches/indexes.

When something goes wrong
-------------------------

Run the diagnostic script and attach its report to a bug report:

  ./pimio-doctor

It writes pimio-doctor-report.txt next to itself and prints a short "LIKELY
CAUSE" summary. The report contains system, layout, library, and Qt plugin
information, and no environment variables beyond the display-related ones it
names, so it is safe to paste into a public issue.

Report problems at https://github.com/dtmland/pimio/issues
