#pragma once

#include "field_set.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace pimio::metadata {

/// Reads what pimio needs from a still image's own bytes.
///
/// The parsers are strictly bounds-checked and never trust a length or offset
/// found in the file: every value is validated against the buffer before it is
/// used. Metadata comes from files pimio did not create, so a hostile or simply
/// broken file must fail as a warning rather than as an out-of-bounds read.
///
/// \a warnings collects recoverable damage, such as a corrupt EXIF block in an
/// otherwise readable image. A returned false means the bytes are not the
/// format at all.
bool readJpeg(const QByteArray &bytes, FieldSet *fields, QStringList *warnings);

bool readPng(const QByteArray &bytes, FieldSet *fields, QStringList *warnings);

/// Reads a bare TIFF file, which also covers TIFF-based RAW containers.
bool readTiff(const QByteArray &bytes, FieldSet *fields, QStringList *warnings);

/// Reads an EXIF TIFF block: the payload after the "Exif\0\0" introducer.
/// Exposed separately because the same block appears inside JPEG, TIFF, and
/// (later) other containers.
bool readExifTiffBlock(const QByteArray &tiff, FieldSet *fields, QStringList *warnings);

/// Converts an EXIF orientation value (1-8) to pimio's clockwise display
/// rotation in degrees. Mirrored orientations contribute their rotation
/// component; the mirroring itself is an edit recipe concern, not metadata.
int rotationForExifOrientation(int orientation);

} // namespace pimio::metadata
