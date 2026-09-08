#pragma once

#include "pimio/editing/image_recipe_renderer.h"

namespace pimio::editing {

/// Creates a persisted derivative record for an exported recipe render.
class ImageExportService
{
public:
    std::optional<core::MediaRecord> exportImage(const core::MediaRecord &source,
                                                 const QString &sourcePath,
                                                 const QString &destinationPath,
                                                 core::Error *error) const;

private:
    ImageRecipeRenderer m_renderer;
};

} // namespace pimio::editing
