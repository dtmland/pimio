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
    sudo dnf install xcb-util-cursor mesa-libGL mesa-libEGL libxkbcommon-x11 pipewire-libpulse

libxcb-cursor0 is required by Qt 6.5 and later. Without it the xcb platform
plugin is found but cannot be loaded, and startup fails with
"Could not load the Qt platform plugin \"xcb\" in \"\" even though it was found."

Qt Multimedia also links the PulseAudio client library for Linux video
thumbnailing, so the archive needs libpulse.so.0 even when you are not playing
audio.

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

When something goes wrong
-------------------------

Run the diagnostic script and attach its report to a bug report:

  ./pimio-doctor

It writes pimio-doctor-report.txt next to itself and prints a short "LIKELY
CAUSE" summary. The report contains system, layout, library, and Qt plugin
information, and no environment variables beyond the display-related ones it
names, so it is safe to paste into a public issue.

Report problems at https://github.com/dtmland/pimio/issues
