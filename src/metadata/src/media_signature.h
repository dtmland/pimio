#pragma once

#include <QByteArray>

namespace pimio::metadata {

/// Container formats this reader can interpret, identified by content.
enum class MediaSignature {
    Unknown,
    Jpeg,
    Png,
    Tiff,      ///< Also covers TIFF-based RAW such as DNG, CR2, NEF, ARW.
    IsoBmff,   ///< MP4, MOV, M4V, 3GP and relatives (timed video).
    HeifImage, ///< HEIF-family still images (AVIF, HEIC, HEIF): ISO-BMFF
               ///< containers, but pictures rather than movies.
};

/// Identifies \a header by its leading bytes. A short buffer is not an error;
/// it simply cannot match anything.
MediaSignature signatureOf(const QByteArray &header);

/// Number of leading bytes signatureOf() needs. Enough to cover an ISO base
/// media `ftyp` box header, its major/minor brand, and several compatible
/// brands, so HEIF-family image brands can be told apart from movie brands.
constexpr int kSignatureProbeBytes = 32;

/// True when \a fileName carries an extension pimio treats as media. Used only
/// to decide whether an unrecognized file is worth reporting as unsupported
/// media; it never decides how a file is parsed.
bool hasMediaExtension(const QString &fileName);

} // namespace pimio::metadata
