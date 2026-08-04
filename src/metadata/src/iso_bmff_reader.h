#pragma once

#include "field_set.h"

#include <QByteArray>
#include <QStringList>

namespace pimio::metadata {

/// Reads duration, track presence, and display dimensions from an ISO base
/// media file (MP4, MOV, M4V and relatives).
///
/// Only the container header is parsed; no samples are decoded, so an
/// undecodable but structurally valid file still yields the facts a library
/// needs. Every box length is validated against the buffer before it is
/// followed, and nesting depth is bounded, so a malformed file cannot drive
/// unbounded recursion.
///
/// Returns false when the bytes are not an ISO base media file at all.
bool readIsoBmff(const QByteArray &bytes, FieldSet *fields, QStringList *warnings);

/// Reads a HEIF-family still image (AVIF, HEIC, HEIF). These share the ISO base
/// media container with movies but hold a picture: no timed track, so duration
/// and audio are absent by design. Pixel dimensions are taken from the item
/// property container when present. Returns false when the bytes are not an ISO
/// base media file at all.
bool readHeifImage(const QByteArray &bytes, FieldSet *fields, QStringList *warnings);

} // namespace pimio::metadata
