#pragma once

#include <QByteArray>

namespace pimio::metadata {

/// Container formats this reader can interpret, identified by content.
enum class MediaSignature {
    Unknown,
    Jpeg,
    Png,
    Tiff,      ///< Also covers TIFF-based RAW such as DNG, CR2, NEF, ARW.
    IsoBmff,   ///< MP4, MOV, M4V, 3GP and relatives.
};

/// Identifies \a header by its leading bytes. A short buffer is not an error;
/// it simply cannot match anything.
MediaSignature signatureOf(const QByteArray &header);

/// Number of leading bytes signatureOf() needs.
constexpr int kSignatureProbeBytes = 16;

/// True when \a fileName carries an extension pimio treats as media. Used only
/// to decide whether an unrecognized file is worth reporting as unsupported
/// media; it never decides how a file is parsed.
bool hasMediaExtension(const QString &fileName);

} // namespace pimio::metadata
