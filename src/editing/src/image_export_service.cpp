#include "pimio/editing/image_export_service.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace pimio::editing {

std::optional<core::MediaRecord> ImageExportService::exportImage(const core::MediaRecord &source,
                                                                  const QString &sourcePath,
                                                                  const QString &destinationPath,
                                                                  core::Error *error) const
{
    if (source.metadata.kind != core::MediaKind::Image) {
        if (error) {
            *error = core::Error(core::ErrorCode::UnsupportedMedia,
                                 QStringLiteral("Only image recipes can be exported in Increment 8."));
        }
        return std::nullopt;
    }
    if (!m_renderer.exportImage(sourcePath, source.recipe, destinationPath, error)) {
        return std::nullopt;
    }

    QFile output(destinationPath);
    if (!output.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = core::Error(core::ErrorCode::Internal,
                                 QStringLiteral("Export completed but cannot be reread."));
        }
        return std::nullopt;
    }
    core::MediaRecord derivative;
    derivative.id = core::MediaId::generate();
    derivative.fingerprint = core::ContentFingerprint(
            QStringLiteral("sha256"),
            QString::fromLatin1(QCryptographicHash::hash(output.readAll(),
                                                          QCryptographicHash::Sha256).toHex()));
    const QFileInfo info(destinationPath);
    derivative.identity.absolutePath = info.absoluteFilePath();
    derivative.identity.sizeBytes = info.size();
    derivative.identity.lastModified = info.lastModified();
    derivative.metadata = source.metadata;
    derivative.metadata.fileName = info.fileName();
    derivative.metadata.folderPath = info.path();
    derivative.metadata.rotationDegrees = 0;
    derivative.recipe = {};
    derivative.derivative = core::DerivativeRelationship{
            source.id, source.recipe.revision(), QStringLiteral("export")};
    return derivative;
}

} // namespace pimio::editing
