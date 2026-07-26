pimio for macOS
===============

These builds are for Apple Silicon (arm64) only. There is no Intel build; see
docs/supported-platforms.md in the source tree for why.

What is in this archive
-----------------------

  pimio            launcher script -- start the application with this
  pimio-doctor     diagnostic script -- run this when something goes wrong
  pimio.app        the application bundle, with Qt frameworks and plugins inside

How to run it
-------------

  1. Extract the archive anywhere you can write, for example
     tar -xzf pimio-<tag>-macOS-binaries.tar.gz -C ~/pimio
  2. Run the launcher, or open pimio.app from Finder:
     ~/pimio/pimio

Keep pimio.app intact. Everything the application needs lives inside the
bundle; moving files out of Contents/ breaks the plugin and framework search
paths.

Gatekeeper
----------

These builds are not code signed or notarized, so macOS quarantines them after
download and refuses to open them ("pimio is damaged" or "cannot be opened
because the developer cannot be verified"). Clear the quarantine attribute
once, after you have satisfied yourself the download is genuine:

  xattr -dr com.apple.quarantine ~/pimio/pimio.app

You can also right-click the bundle, choose Open, and confirm the dialog.

When something goes wrong
-------------------------

Run the diagnostic script and attach its report to a bug report:

  ./pimio-doctor

It writes pimio-doctor-report.txt next to itself and prints a short "LIKELY
CAUSE" summary. The report includes the bundle layout, otool dependency output,
the quarantine attribute, and the code signature status, and no environment
variables beyond the display-related ones it names, so it is safe to paste
into a public issue.

Report problems at https://github.com/dtmland/pimio/issues
