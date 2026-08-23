# 0004 — Showing a library while it is still being scanned, and who owns a thumbnail

Status: **accepted.** Recorded while fixing the "grey tiles" report.

Decision owner: repository maintainer.

## Decision

Two rules, one for each half of the problem.

1. **A scan publishes what it has finished, in batches.** `scan::Scanner` takes
   a commit batch size and a progress callback. When the batch fills, the
   scanner commits it to the durable store and hands the committed records to
   the caller, which projects them and reloads the model, so tiles appear while
   the scan is still walking the tree.
2. **The model owns thumbnail retention; the image provider is only a
   handover buffer.** `MediaLibraryModel` keeps a bounded most-recently-used
   list of the media ids whose thumbnails it is willing to claim are `Ready`.
   When an id falls off the end, the model removes it from the provider *and*
   puts the row back to `Pending` in the same step. The provider's own cache is
   sized above the model's bound so it never evicts on its own.

## Context

Two symptoms were reported and they turned out to share a cause: nobody was
tracking what the thumbnail image provider actually held.

**Nothing appeared until the scan finished.** `Scanner::scan()` staged every
record and committed once at the end, and the projection was only rebuilt when
the scan job succeeded. On a large library that is minutes of an apparently
frozen window.

**Thumbnails decayed into grey tiles.** The provider held a
`QCache<QString, QImage>` bounded at 512 entries. The model marked a row
`Ready` when the render finished and never revisited that. Past the 513th
thumbnail the cache silently dropped its least-recently-used entries, and the
rows they belonged to still said `Ready`, so QML asked for
`image://thumbnail/<id>`, got nothing, and logged

```
QML QQuickImage: Failed to get image from provider: image://thumbnail/<id>
```

Nothing ever re-requested those rows, so the tiles stayed grey for the rest of
the session. It looked intermittent and it looked like whole contiguous bands,
because that is exactly what an LRU eviction of a scrolled window looks like:
the ends of the library survived because they had been visited most recently.

## Why the model, and not the provider

The provider is reachable from the Qt Quick render thread and knows nothing
about rows; the model knows what is on screen and can emit `dataChanged`. Only
one of the two can put a row back to `Pending`, so retention has to be decided
where that is possible. Making the bound the model's also means there is one
number to reason about instead of two that can disagree — the failure above was
precisely two bounds disagreeing.

The bound is never smaller than what the current window can ask for
(visible rows + twice the prefetch margin + slack). A bound smaller than the
window would drop a thumbnail the grid is displaying and immediately ask for it
again. If a still-visible row does get evicted — the window shrank, say — it is
re-requested in the same step rather than left showing a placeholder.

As a belt-and-braces measure the QML delegate retries **once** per source when
an `Image` reports `Image.Error`, calling `refreshThumbnail(row)`. Once, so a
file that genuinely cannot be rendered does not become an endless loop.

## Why batches are committed rather than merely reported

A batch is written to the durable store before it is announced. The alternative
— announcing staged-but-uncommitted records — would show the user rows that a
crash would erase. Committing more often costs more LORE revisions; the
`scanBatchSize` stored setting (8–2048, default 64) is that trade, exposed
because the right answer depends on the library and the disk. Setting it to 0
in code restores the single-commit behaviour, which is what non-interactive
callers get by default.

A cancelled scan keeps the batches it already committed. They describe files
that really are on disk, the scan is idempotent, and the next run converges on
the rest; undoing them would only mean rescanning work that was correct.

## Why a batch does not advance the projection's state token

`ProjectionDatabase::applyRecords()` writes the batch in one transaction but
deliberately leaves the projected state token alone. The durable store remains
the ground truth and the projection remains something that can be thrown away
and rebuilt; only a full `rebuildFrom()` may claim the projection matches the
store. Keeping the token honest means `isStale()` still answers correctly if
pimio is killed mid-scan — the next launch rebuilds rather than trusting a
partial projection.

## Cost

- More LORE revisions per scan (one per batch instead of one per scan).
- A model reload per batch, coalesced by a 250 ms timer so a fast scanner
  cannot force more than about four reloads a second. A reload that only adds
  rows uses `beginInsertRows` at their sorted positions, not a model reset, so
  the grid keeps its scroll position and its loaded thumbnails even when a
  later batch sorts before or between rows already shown.
- A bounded amount of thumbnail re-rendering when a user scrolls further than
  the retention bound and comes back. That work is the point: it is what makes
  the tile appear instead of staying grey.

## What would reverse this

- A thumbnail cache that is durable and shared with the render thread would
  make retention a cache-lookup question rather than a model-state question,
  and this record should then be revisited.
- If per-batch reloads ever cost more than the responsiveness is worth on very
  large libraries, the model would need an incremental "apply these ids" path
  instead of re-reading the sorted id list.
