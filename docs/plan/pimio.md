# pimio

**pimio** is a photo organization app focused on helping users build and maintain chronological photo libraries.

## Vision

The app is less about photo touch-ups and more about photo organization. Its primary goal is to help users take large sets of photos and organize them accurately in time, even when original metadata is missing, incomplete, or incorrect.

The interface should feel fluid and simple, similar in spirit to Google Picasa.

## Core Goals

- Emphasize chronological photo and video organization
- Make it easy to work with large photo libraries
- Provide best-effort tools for reconstructing or correcting timestamps
- Keep advanced metadata capabilities available without overwhelming the user
- Support both images and videos throughout the app

## Key Features

### Chronological Organization

- Create and manage photo libraries centered around time-based organization
- Allow users to reorder a photo or group of photos by dragging and dropping them within a chronological timeline
- Detect groups of photos that may be related by time or location to assist with organization
- Provide tools to apply best-effort timestamps when original metadata is missing or incorrect
- Optionally apply timezone offsets to datetimes using GPS location and timezone data, including correct DST handling based on place and year

### Location and Mapping

- Include a map view for GPS tagging of both images and videos
- Support landmark detection to help infer or refine GPS location tagging
- Explore support for videos with multiple GPS coordinates over time, if a suitable metadata standard exists

### Media Editing

Support basic editing features for both images and videos, including:

- Cropping
- Rotating
- Creating duplicates

For video specifically:

- Clip large videos into shorter cuts
- Support automatic scene detection to assist the clipping process

### Metadata Experience

- Read and write a rich set of metadata types
- Keep metadata presentation simple by default
- Allow users to inspect and edit deeper metadata only when they want to

### Intelligent Assistance

- Automatically detect groups of related photos based on time or space
- Support some level of offline facial detection
- Allow face tracking over time, including from younger to older appearances, to help with timestamping and organization
- Support some level of offline landmark detection to aid location tagging

## Storage and File Management

The core file store may rely on one or both of the following approaches:

- A design inspired by Google Picasa’s `.ini`-based metadata storage
- A more advanced system built on the LORE version control system, running both server and client components while remaining abstracted from the user

## Export

- Provide export options that preserve or adapt timestamps correctly for platforms like:
  - Google Photos
  - iCloud Photo Library

## Design Principles

- Simple, fluid, and approachable UI
- Organization-first rather than editing-first
- Powerful metadata handling without exposing unnecessary complexity
- Best-effort automation that helps users recover and organize imperfect archives

## Open Questions

- What is the best metadata standard for storing multiple GPS coordinates across a video's timeline?
- How much facial recognition and landmark detection can be done effectively offline?
- Should the storage system begin with a simple `.ini`-style approach and later expand into a LORE-backed architecture?
