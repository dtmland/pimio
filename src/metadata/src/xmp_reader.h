#pragma once

#include "field_set.h"

#include <QByteArray>
#include <QStringList>

namespace pimio::metadata {

/// Reads the fields pimio understands from an XMP packet.
///
/// XMP is XML, so it is parsed with a streaming parser and never with string
/// matching. Unknown properties are ignored rather than guessed at, and a
/// packet that does not parse is reported through \a warnings instead of
/// failing the media file it belongs to: a bad sidecar must not hide a good
/// photo.
///
/// Returns false when \a xmp is not a parseable XMP packet.
bool readXmpPacket(const QByteArray &xmp, FieldSet *fields, QStringList *warnings);

} // namespace pimio::metadata
