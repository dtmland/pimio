#include "pimio/editing/image_recipe_renderer.h"

#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QTransform>

namespace pimio::editing {
namespace {

void setError(core::Error *error, core::ErrorCode code, const QString &message,
              const QString &path)
{
    if (error) {
        *error = core::Error(code, message).withContext({{QStringLiteral("path"), path}});
    }
}

bool applyOperation(QImage *image, const core::EditOperation &operation, core::Error *error)
{
    const QJsonObject parameters = operation.parameters();
    switch (operation.kind()) {
    case core::EditOperationKind::Crop: {
        const QJsonValue x = parameters.value(QStringLiteral("x"));
        const QJsonValue y = parameters.value(QStringLiteral("y"));
        const QJsonValue width = parameters.value(QStringLiteral("width"));
        const QJsonValue height = parameters.value(QStringLiteral("height"));
        if (!x.isDouble() || !y.isDouble() || !width.isDouble() || !height.isDouble()) {
            setError(error, core::ErrorCode::Conflict,
                     QStringLiteral("Crop needs numeric x, y, width, and height."), {});
            return false;
        }
        const QRect crop(x.toInt(), y.toInt(), width.toInt(), height.toInt());
        if (crop.isEmpty() || !QRect(QPoint(), image->size()).contains(crop)) {
            setError(error, core::ErrorCode::Conflict,
                     QStringLiteral("Crop is outside the source image."), {});
            return false;
        }
        *image = image->copy(crop);
        return true;
    }
    case core::EditOperationKind::Rotate: {
        const QJsonValue degreesValue = parameters.value(QStringLiteral("degrees"));
        const int degrees = degreesValue.toInt();
        if (!degreesValue.isDouble() || degrees % 90 != 0) {
            setError(error, core::ErrorCode::Conflict,
                     QStringLiteral("Rotation must be a multiple of 90 degrees."), {});
            return false;
        }
        *image = image->transformed(QTransform().rotate(degrees));
        return true;
    }
    case core::EditOperationKind::Orientation: {
        const QJsonValue orientationValue = parameters.value(QStringLiteral("value"));
        const int orientation = orientationValue.toInt();
        QTransform transform;
        if (!orientationValue.isDouble()) {
            setError(error, core::ErrorCode::Conflict,
                     QStringLiteral("Orientation needs an EXIF value from 1 to 8."), {});
            return false;
        }
        switch (orientation) {
        case 1: break;
        case 2: transform.scale(-1, 1); break;
        case 3: transform.rotate(180); break;
        case 4: transform.scale(1, -1); break;
        case 5: transform.rotate(90).scale(-1, 1); break;
        case 6: transform.rotate(90); break;
        case 7: transform.rotate(270).scale(-1, 1); break;
        case 8: transform.rotate(270); break;
        default:
            setError(error, core::ErrorCode::Conflict,
                     QStringLiteral("Orientation must be an EXIF value from 1 to 8."), {});
            return false;
        }
        *image = image->transformed(transform);
        return true;
    }
    case core::EditOperationKind::Unknown:
    case core::EditOperationKind::Trim:
        setError(error, core::ErrorCode::UnsupportedMedia,
                 QStringLiteral("This recipe contains an operation that cannot render an image."), {});
        return false;
    }
    return false;
}

QImage render(const QString &sourcePath, const core::EditRecipe &recipe, core::Error *error)
{
    if (!recipe.isFullyRecognized()) {
        setError(error, core::ErrorCode::UnsupportedMedia,
                 QStringLiteral("This recipe contains a newer operation."), sourcePath);
        return {};
    }
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        setError(error, QImageReader::imageFormat(sourcePath).isEmpty()
                         ? core::ErrorCode::UnsupportedMedia : core::ErrorCode::CorruptData,
                 QStringLiteral("Cannot decode image: %1").arg(reader.errorString()), sourcePath);
        return {};
    }
    for (const core::EditOperation &operation : recipe.operations()) {
        if (!applyOperation(&image, operation, error)) {
            return {};
        }
    }
    return image;
}

} // namespace

QImage ImageRecipeRenderer::preview(const QString &sourcePath, const core::EditRecipe &recipe,
                                    const QSize &targetSize, core::Error *error) const
{
    QImage image = render(sourcePath, recipe, error);
    // targetSize is a maximum preview bound: never upscale a smaller recipe result
    // (for example a 1x1 crop) just to fill the requested box.
    if (!image.isNull() && targetSize.isValid() && !targetSize.isEmpty()
        && (image.width() > targetSize.width() || image.height() > targetSize.height())) {
        image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

bool ImageRecipeRenderer::exportImage(const QString &sourcePath, const core::EditRecipe &recipe,
                                      const QString &destinationPath, core::Error *error) const
{
    if (destinationPath.isEmpty() || QFileInfo(destinationPath).absoluteFilePath()
                                         == QFileInfo(sourcePath).absoluteFilePath()) {
        setError(error, core::ErrorCode::Conflict,
                 QStringLiteral("Export destination must differ from the original."), destinationPath);
        return false;
    }
    const QImage image = render(sourcePath, recipe, error);
    if (image.isNull()) {
        return false;
    }
    const QByteArray format = QFileInfo(destinationPath).suffix().toLatin1();
    if (format.isEmpty()) {
        setError(error, core::ErrorCode::UnsupportedMedia,
                 QStringLiteral("Export destination needs an image extension."), destinationPath);
        return false;
    }
    QSaveFile output(destinationPath);
    if (!output.open(QIODevice::WriteOnly)) {
        setError(error, output.error() == QFileDevice::PermissionsError
                         ? core::ErrorCode::PermissionDenied : core::ErrorCode::OutOfSpace,
                 QStringLiteral("Cannot create export: %1").arg(output.errorString()), destinationPath);
        return false;
    }
    QImageWriter writer(&output, format);
    if (!writer.write(image) || !output.commit()) {
        output.cancelWriting();
        setError(error, core::ErrorCode::OutOfSpace,
                 QStringLiteral("Cannot atomically write export: %1").arg(writer.errorString()),
                 destinationPath);
        return false;
    }
    return true;
}

} // namespace pimio::editing
