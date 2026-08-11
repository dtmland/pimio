# 0003 — Settings, and the browsing controls that read them

Status: **accepted.** Recorded during Increment 7.5, which delivers browsing
controls and the settings they read.

Decision owner: repository maintainer.

## Decision

pimio has one settings object, `pimio::settings::Settings`, and every setting
in it is one of two kinds:

| Kind | Where it lives | Lifetime | Example |
| --- | --- | --- | --- |
| **Stored setting** | `pimio.conf` | Survives restart | Tile size, sort order, scroll speed |
| **Session setting** | Memory only | Reset at every launch | Tile diagnostics overlay |

The names are deliberate and are used verbatim in the UI, in the code, and in
this documentation: a **stored setting** is remembered, a **session setting**
is not. The settings dialog groups them under "Stored — remembered for next
time" and "This session only — back to normal next launch", so the distinction
is visible at the moment of choosing rather than discovered afterwards.

The rule that decides which kind a new setting is:

> A setting is **stored** if a user would be annoyed to set it again tomorrow.
> It is a **session** setting if it answers "what am I doing right now", or if
> waking up to it silently enabled would confuse someone who does not remember
> turning it on.

Diagnostics fall on the session side by that rule; a preferred tile size falls
on the stored side.

The configuration is **application-wide, not per library**. It records how a
person likes to look at photographs, which does not change because they opened
a different folder.

## Context

Increments 0–7 delivered a grid that could only be scrolled with the mouse, at
Qt's default wheel step, in one fixed order, at one fixed tile size. That is
enough to prove the pipeline and not enough to test it: judging thumbnail
quality needs a bigger tile, judging ordering needs another ordering, and
judging a scan of ten thousand files needs a keyboard.

Every one of those controls needs somewhere to keep its value, so the settings
component comes first and the controls are its first consumers.

## Options considered

| Option | Cost | Consequence |
| --- | --- | --- |
| `QSettings` directly wherever a value is needed | None up front | No single list of what pimio remembers; no validation; QML and C++ disagree about defaults; nothing to test |
| Settings inside the LORE durable store | Migrations and revisions for values that are not user data | Preferences would be per library and would sync as if they were content |
| A `Settings` QObject over `QSettings`, injected where needed | One small component | One list of every setting, one place that clamps and validates, Q_PROPERTY bindings for QML, and a test that pins the defaults |

`QSettings` is Qt's own facility, already available, and writes an INI file
that a user can read and a support request can quote. Adding a configuration
library for this would not be justifiable.

## Consequences

Accepted:

- The file lives at `QStandardPaths::AppConfigLocation/pimio.conf` in INI
  format, so it is identical on all three platforms and is not the Windows
  registry. Tests set `QStandardPaths::setTestModeEnabled(true)` so they never
  touch a developer's real configuration.
- Every stored value is clamped and range-checked on read. A hand-edited or
  corrupt file produces the default, never an unusable window.
- Settings is a plain `QObject` with no dependency beyond `Qt6::Core`, so the
  browser, app, and any future component can read it without a cycle.

## Sort keys

Sorting is done by the projection database, not by the model: the database has
the index, and re-sorting a hundred thousand rows in memory would undo the
reason the projection exists. `ProjectionDatabase::idsSorted()` supports
capture time (the default), file name, file date, file type, and file size.

Two consequences worth recording:

- SQLite has no last-index-of function, so "file type" cannot be derived in
  SQL from the file name. The extension is computed on insert and stored in a
  `file_extension` column, lower-cased, with the rule that the last dot wins
  and a leading dot means the file has no extension. Migration 3 adds the
  column, indexes the four new sort columns, and clears the projection's state
  token so the projection rebuilds and populates it.
- Descending reverses the leading columns only. The `id` tie-break always
  ascends, so reversing the sort does not also reshuffle rows that compare
  equal — a photograph does not move relative to its neighbour taken in the
  same second.

## Tile size and thumbnail resolution

The tile-size slider changes the grid live. The ceiling is not arbitrary: a
thumbnail is never upscaled by the renderer, so a tile larger than the cached
thumbnail draws a soft image.

The model requests one of three sizes — **128, 256, 512** — and picks the
smallest that still covers the tile in device pixels. The tile slider runs from
96 to 256 device-independent pixels, so on a 2× display the largest tile needs
512 device pixels, which is exactly the largest tier. Nothing larger is offered
because nothing in the grid could use it; the preview loads the original file.

This is why the default thumbnail size rose from 160 to 256: the old default
was already being upscaled slightly into the 176-pixel tile that shipped.

The cost is cache size, and it is bounded. Each size is a separate cache entry
because `MediaRequest::cacheKey()` includes the target size, so a user who
moves the slider across all three tiers eventually holds three thumbnails per
photograph. At roughly 8/30/110 KB per JPEG thumbnail for the three tiers, a
100,000-item library that has visited every tier occupies about 15 GB against
the default 512 MB budget — so the LRU trim, not the tier count, decides what
survives, and the tiers are few enough that the working set for one tile size
stays resident.

## What would reverse this

- A setting that must be per library rather than per user — a per-library sort
  order, say — would need a second store, and this record should then be
  superseded rather than edited.
- A grid that draws tiles larger than 256 device-independent pixels would need
  a fourth tier and a re-examination of the cache budget.
