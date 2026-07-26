#pragma once

#include "pimio/core/error.h"
#include "pimio/core/file_system.h"
#include "pimio/core/types.h"

#include <QByteArray>
#include <QString>

namespace pimio::scan {

/// Computes content fingerprints from raw bytes.
///
/// The algorithm is SHA-256. The digest is hex-encoded. Both values are stable
/// across pimio releases; changing either would invalidate all thumbnail and
/// duplicate caches.
class MediaHasher
{
public:
    static core::ContentFingerprint computeFingerprint(const QByteArray &data);

    /// Reads \a path from \a fs and returns its fingerprint, or a null
    /// fingerprint when the file cannot be read. Sets \a error on failure.
    static core::ContentFingerprint fingerprintFile(const QString &path,
                                                    core::FileSystem &fs,
                                                    core::Error *error);
};

} // namespace pimio::scan
