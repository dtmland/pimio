#include "pimio/scan/media_hasher.h"

#include <QCryptographicHash>

namespace pimio::scan {

core::ContentFingerprint MediaHasher::computeFingerprint(const QByteArray &data)
{
    const QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return core::ContentFingerprint(QStringLiteral("sha256"),
                                    QString::fromLatin1(digest.toHex()));
}

core::ContentFingerprint MediaHasher::fingerprintFile(const QString &path,
                                                      core::FileSystem &fs,
                                                      core::Error *error)
{
    core::Error readError;
    const QByteArray data = fs.readAll(path, &readError);
    if (readError.isError()) {
        if (error != nullptr) {
            *error = readError;
        }
        return {};
    }
    return computeFingerprint(data);
}

} // namespace pimio::scan
