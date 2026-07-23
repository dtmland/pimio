# pimio 2.0.0 Release Plan

## Release Goal

pimio 2.0.0 replaces the basic 1.0.0 browsing and preview views with a fluid,
GPU-accelerated media experience while retaining the proven library, metadata,
job, edit, and export services from 1.0.0. It should be driven by measured
performance limitations, not by a requirement to reproduce historical Picasa
implementation details.

Qt's rendering abstraction should select the appropriate platform backend:
Direct3D on Windows, Metal on macOS, and Vulkan or OpenGL on Linux. Keep
platform-specific integrations behind adapters rather than adopting a
lowest-common-denominator frontend.

## Rendering and Browsing

- Replace standard QML grid/timeline delegates where needed with custom QML
  scene-graph components that consume the existing media-query and
  thumbnail/preview-request interfaces.
- Provide GPU texture management, virtualized tile rendering, viewport-aware
  prefetching, and strict eviction limits.
- Add smooth pan, zoom, transition, and selection behavior across the library,
  timeline, map, and viewer.
- Generate and request image pyramids or deep-zoom tiles only for media and
  zoom levels where they improve measured responsiveness.
- Use preview-first loading: thumbnail or embedded preview, then a
  color-managed medium preview, then full-resolution source or tiles as needed.
- Apply reduced-image edit previews through the GPU where beneficial; debounce
  controls and cancel obsolete preview work.
- Keep fallback behavior for systems where the preferred graphics backend is
  unavailable.

## Advanced Maps and Location Workflows

"Advanced maps" means capabilities beyond 1.0.0's basic GPS display and manual
location assignment:

- Offline map-tile caching and controlled cache eviction.
- Clustering and filtering of very large libraries on a map.
- Batch location correction and richer timeline-to-map interactions.
- Reverse geocoding and landmark-assisted location suggestions, subject to
  privacy, offline-data, and licensing decisions.
- Visualization and editing of video GPS tracks when a reliable interoperable
  metadata representation is available.
- Clear confidence and conflict handling for inferred locations.

All location intelligence remains optional, local where possible, and subject
to user confirmation before metadata changes.

## Deferred Intelligence and Editing

- Add opt-in local face detection and, only after licensing review, identity
  clustering. Record the license and redistribution rights of model weights
  separately from those of inference code.
- Offer removable local analysis data, pause controls, and battery/thermal
  limits.
- Evaluate additional non-destructive filters, lens correction, and darkroom
  controls only when their recipes can render consistently in preview and
  export paths.
- Evaluate opt-in sync providers separately from indexing. Synchronize
  originals and portable sidecars conflict-aware, and rebuild derived caches
  locally rather than syncing caches.

## Continuity Requirements

- Do not change the portable metadata, edit-recipe, persistent-job, or export
  contracts merely to support the accelerated frontend.
- Continue to treat watchers as hints and reconciliation scans as the source of
  recovery from missed events.
- Preserve cancellation, bounded resource use, and foreground-work priority
  throughout the new renderer.
- Keep all generated thumbnails, pyramids, map tiles, and GPU caches disposable
  and versioned.
- Test accessibility, native menus, keyboard navigation, drag and drop, HiDPI,
  and fallback rendering alongside visual performance.

## Release Acceptance

1. Scrolling, zooming, and opening media are measurably more responsive than
   1.0.0 on the lowest supported hardware.
2. Large-library texture and cache use remains bounded and recoverable.
3. Custom views use the established 1.0.0 services without duplicating indexing,
   metadata, editing, or job logic.
4. Advanced maps and optional intelligence are safe to pause, remove, and
   recover from without endangering the library.
5. The accelerated frontend and fallbacks are tested natively on supported
   Windows, macOS, and Linux graphics environments.
