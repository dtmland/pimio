add_library(pimio_editing STATIC
    include/pimio/editing/image_export_service.h
    include/pimio/editing/image_recipe_renderer.h
    include/pimio/editing/metadata_edit_service.h
    src/image_export_service.cpp
    src/image_recipe_renderer.cpp
    src/metadata_edit_service.cpp
)

add_library(pimio::editing ALIAS pimio_editing)

target_include_directories(pimio_editing PUBLIC include)
target_link_libraries(pimio_editing PUBLIC pimio::core Qt6::Core Qt6::Gui)
