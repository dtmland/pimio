pimio for Windows
=================

What is in this archive
-----------------------

  pimio.bat            launcher -- start the application with this
  pimio-doctor.ps1     diagnostic script -- run this when something goes wrong
  bin\                 pimio.exe, qt.conf, the Qt DLLs, and lore.dll
  bin\vc_redist.x64.exe the Microsoft Visual C++ runtime installer
  plugins\             Qt plugins, including plugins\platforms
  qml\                 QML modules the application imports
  translations\        Qt's own translations

How to run it
-------------

  1. Extract the whole ZIP anywhere you can write, for example
     C:\Users\<you>\pimio
  2. Double-click pimio.bat, or run it from a terminal:
     C:\Users\<you>\pimio\pimio.bat

The launcher works from any working directory. You can also run bin\pimio.exe
directly, but only from the extracted directory itself: Qt finds its plugins
through bin\qt.conf, whose Prefix is relative to the executable's location.

Keep the whole tree together. Do not move pimio.exe, the plugins directory, or
individual DLLs somewhere else; the layout is what makes the archive
self-contained.

Windows may show a SmartScreen warning because these builds are not code
signed. Choose "More info" and then "Run anyway" if you trust the download.

Visual C++ runtime
------------------

pimio needs the Microsoft Visual C++ 2015-2022 x64 redistributable. Most
machines already have it. If startup fails with a missing VCRUNTIME140.dll or
MSVCP140.dll, run bin\vc_redist.x64.exe once and try again.

When something goes wrong
-------------------------

Run the diagnostic script from PowerShell and attach its report to a bug
report:

  powershell -ExecutionPolicy Bypass -File .\pimio-doctor.ps1

It writes pimio-doctor-report.txt next to itself and prints a short "LIKELY
CAUSE" summary. The report contains system, layout, DLL, and Qt plugin
information, and no environment variables, so it is safe to paste into a
public issue.

Report problems at https://github.com/dtmland/pimio/issues
