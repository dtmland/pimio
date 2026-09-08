#pragma once

#include "pimio/core/durable_store.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace pimio::editing {

/// Decodes an image and replays the portable edit recipe without writing source bytes.
class ImageRecipeRenderer
{
public:
    /// Renders a recipe preview. When targetSize is set it is treated as a maximum
    /// bound: the image is downscaled to fit, never upscaled.
    QImage preview(const QString &sourcePath, const core::EditRecipe &recipe,
                   const QSize &targetSize, core::Error *error) const;
    bool exportImage(const QString &sourcePath, const core::EditRecipe &recipe,
                     const QString &destinationPath, core::Error *error) const;
};

} // namespace pimio::editing
