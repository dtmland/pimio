qt_add_library(pimio_browser STATIC
    include/pimio/browser/media_library_model.h
    include/pimio/browser/thumbnail_image_provider.h
    src/media_library_model.cpp
    src/thumbnail_image_provider.cpp
)

add_library(pimio::browser ALIAS pimio_browser)

target_include_directories(pimio_browser PUBLIC include)

target_link_libraries(pimio_browser PUBLIC
    pimio::core
    pimio::projection
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
)
