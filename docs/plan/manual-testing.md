# Manual Testing Plan (Field Notes — Tests C)

This document is the **Field Notes** tier described in
[../testing.md](../testing.md): the tests that cannot be automated in the
current CI environment. It records the conditions, steps, and acceptance
criteria for each. Automated evidence is listed in
[pimio-v1-implementation.md](pimio-v1-implementation.md) alongside each
increment. Items here are complementary, not replacements for automated
checks, and an entry stays here only as a last resort — see
[../testing.md](../testing.md) for the tier rules and the reporting template.

A test entry is complete only when a tester signs it off with the platform,
OS version, Qt version, hardware, and result.

---

## Increment 6 — Thumbnails, Models, and Basic Browser

The shipped application accepts one or more `--library <path>` options and
wires those roots to the real projection, scan, thumbnail, and watch services.
All entries below are runnable from a normal launch.

### MT-6.1 — QML grid renders thumbnails on screen

**Status**: Ready for sign-off.

**Condition**: Real display server (not offscreen); Qt 6.4 or newer with
QtQuick, QtQuick.Controls, and QtQuick.Window modules installed.

**Steps**

1. Build and launch `pimio`.
2. Open a library root containing JPEG and PNG images.
3. Let the application scan and index the library.
4. Observe the main grid view.

**Acceptance**

- The grid shows a tile for every indexed item.
- Each tile transitions from a placeholder ("…") to the rendered thumbnail
  as the `ThumbnailService` completes requests.
- The placeholder is visible and correctly sized before the thumbnail arrives.
- Items that fail to render show "Error" text rather than crashing or showing
  a broken tile.
- Scrolling does not produce visible lag or flicker.

**Platforms**: Linux (X11/Wayland), macOS, Windows

---

### MT-6.2 — Thumbnail requests respect the visible window

**Status**: Ready for sign-off after MT-6.1.

**Condition**: Same as MT-6.1.

**Steps**

1. Open a library with at least 200 items.
2. Observe the network/CPU activity while the grid is at rest.
3. Scroll quickly to the middle of the library.
4. Observe the thumbnail request activity.

**Acceptance**

- While the view is at rest, only visible tiles and the prefetch margin
  (±20 rows by default) have in-flight requests.
- After a fast scroll, requests for the previous window are cancelled promptly;
  new requests begin for the new visible range.
- The activity log from the process shows `cancelAllExcept` being called on
  scroll, not just new requests being added.

---

### MT-6.3 — Empty-library placeholder

**Condition**: Launch the application without any library configured.

**Steps**

1. Start `pimio` with a fresh profile (no library roots).
2. Observe the main window.

**Acceptance**

- The toolbar banner shows the same `pimio <version>` string as `pimio --version`
  for the build being tested.
- A drawn camera icon (not an emoji glyph, which is missing on some Linux
  systems) and the text "No media yet. Launch with --library <folder> to scan
  and browse a library." are centred in the window.
- No grid rows, no thumbnail requests, no errors in the console.

---

### MT-6.4 — Corrupt cache entry is silently replaced

**Status**: Ready for sign-off after MT-6.1.

**Condition**: Real display server.

**Steps**

1. Open a library and wait for thumbnails to appear.
2. Locate the cache directory (typically `~/.cache/pimio/thumbnails/` or the
   platform equivalent).
3. Replace one entry with a zero-byte file.
4. Scroll that item out of view and back in (to trigger a fresh request).

**Acceptance**

- The item transitions back through "Loading" and eventually shows the correct
  thumbnail.
- No crash or error dialog.

---

### MT-6.5 — Large library performance baseline

**Status**: Ready for sign-off after MT-6.1.

**Condition**: Library of ≥ 10 000 items; real display server.

**Steps**

1. Import a library of at least 10 000 images.
2. Time how long until the grid is interactive (not frozen).
3. Scroll through the entire library at a moderate pace.

**Acceptance**

- The grid becomes interactive within 2 seconds of the application launching
  (IDs are loaded from the projection; thumbnails are loaded lazily).
- Scrolling does not drop below 30 fps on a mid-range machine.
- Peak memory growth during scrolling stabilises rather than growing without
  bound.

---

### MT-6.6 — Hardware-accelerated rendering (GPU)

**Status**: Ready for sign-off after MT-6.1.

**Condition**: Machine with a discrete GPU; real display server.

**Steps**

1. Launch `pimio` with a library.
2. Monitor GPU utilisation during grid scrolling.

**Acceptance**

- Qt Scene Graph uses the GPU for compositing the grid.
- No OpenGL errors appear in the console.
- Rendering is visually smooth and does not fall back to software rendering
  unexpectedly.

---

### MT-6.7 — Progressive image detail

**Status**: Ready for sign-off after MT-6.1.

**Condition**: A real display server and a library containing a large image.

**Steps**

1. Wait for the image's grid thumbnail to appear.
2. Select the image tile.
3. Observe the detail view while the original image loads.
4. Press Escape, then reopen the item and use the Close button.

**Acceptance**

- The detail view opens immediately with a loading indicator while the original
  image is decoded asynchronously.
- The full image is fitted without changing its aspect ratio.
- The path and capture time remain readable below the preview.
- Escape and the Close button both return focus to the grid.

---

## Increment 7.5 — Browsing Controls and Settings

Automated tests cover what these controls compute — the step a held key
produces, the distance a wheel notch scrolls, the thumbnail tier a tile size
selects. What they cannot judge is how it feels, which is the whole point of
the increment.

### MT-7.5.1 — Scroll feel with a real wheel and a real touchpad

**Status**: Ready for sign-off.

**Condition**: Real display server; a mouse with a notched wheel *and* a
touchpad or high-resolution wheel, on the same machine if possible.

**Steps**

1. Launch `pimio` with a library of at least 500 items.
2. Scroll with the notched wheel: one notch at a time, then a fast continuous
   spin.
3. Scroll with the touchpad using the same two gestures.
4. Open Settings, halve and then double the scroll speed, and repeat.
5. Turn scroll acceleration off and repeat the fast continuous scroll.

**Acceptance**

- One notch moves a useful distance — noticeably more than half a tile row —
  without overshooting.
- A fast continuous scroll speeds up smoothly and stops speeding up at a rate
  the tester can still read.
- Pausing for about a second and scrolling again starts slow: the previous
  gesture's speed is not inherited.
- The touchpad feels like the content is being dragged, not stepped.
- With acceleration off, every notch moves the same distance.

**Platforms**: Linux (X11/Wayland), macOS, Windows

---

### MT-7.5.2 — Key-hold acceleration in the grid and the preview

**Status**: Ready for sign-off.

**Condition**: Real display server. The OS keyboard repeat rate is at its
default; note the value if it is not.

**Steps**

1. Launch with a library of at least 500 items.
2. Tap Down once, then hold Down for about five seconds, then release.
3. Repeat with Up, PageDown, and PageUp.
4. Open a preview and hold Right for about five seconds, then Left.
5. Turn key acceleration off in Settings and repeat steps 2 and 4.

**Acceptance**

- A single tap moves exactly one item (one row for Up/Down).
- A hold starts at the same speed and speeds up within a second or so, and
  stops speeding up while still being followable.
- The selection never leaves the library at either end, and never jumps
  backwards.
- In the preview, held arrows keep up: the image being shown is never more
  than a moment behind the key.
- With acceleration off, a held key moves one item per repeat.

**Platforms**: Linux (X11/Wayland), macOS, Windows

---

### MT-7.5.3 — Tile size and thumbnail sharpness

**Status**: Ready for sign-off.

**Condition**: Real display server. Run once on a standard-density display and
once on a HiDPI display if one is available.

**Steps**

1. Launch with a library containing detailed photographs.
2. Drag the tile-size slider from its minimum to its maximum and back, slowly,
   then quickly.
3. Leave it at the maximum and let the grid settle.
4. Restart the application.

**Acceptance**

- The grid re-lays out live while the slider moves, without flicker or a frozen
  window.
- At the maximum tile size, thumbnails are sharp — not visibly softer than at
  the default size.
- After a size change, tiles that were on screen fill in again promptly rather
  than staying on placeholders.
- The size chosen before the restart is the size shown after it.

**Platforms**: Linux (X11/Wayland), macOS, Windows

---

### MT-7.5.4 — Settings persistence and the session/stored split

**Status**: Ready for sign-off.

**Condition**: Real display server.

**Steps**

1. Change every stored setting away from its default, and turn the tile
   diagnostics overlay on.
2. Quit and relaunch.
3. Locate `pimio.conf` (the platform's application configuration directory) and
   read it.
4. Use Reset in the settings dialog, then quit and relaunch.

**Acceptance**

- Every stored setting survives the restart.
- The tile diagnostics overlay is off after the restart.
- `pimio.conf` is readable text, contains the stored settings, and contains no
  entry for the diagnostics overlay.
- After Reset, the application looks as it did on first launch, and still does
  after a restart.

**Platforms**: Linux (X11/Wayland), macOS, Windows

---

## General / Cross-increment

### MT-G.1 — Application does not modify originals during scan

**Condition**: Any library with real media files; verify file modification
times before and after a scan.

**Steps**

1. Record `mtime` values for a representative selection of source files
   (`stat` or equivalent).
2. Run a full scan via the application.
3. Record `mtime` values again.

**Acceptance**

- No source file's `mtime` has changed.
- No new files have been created alongside the source files (no hidden
  sidecars, no `.pimio` files in the library directory).

---

### MT-G.2 — Real-GPU, signing, and installer UX (Release gate)

Covered per [pimio-v1-implementation.md](pimio-v1-implementation.md)
Increment 12. Requires self-hosted runners with real GPU, OS signing
credentials, and representative installer scenarios on each supported
platform.
