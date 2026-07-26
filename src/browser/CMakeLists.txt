qt_add_library(pimio_browser STATIC
    include/pimio/browser/media_library_model.h
    src/media_library_model.cpp
)

add_library(pimio::browser ALIAS pimio_browser)

target_include_directories(pimio_browser PUBLIC include)

target_link_libraries(pimio_browser PUBLIC
    pimio::core
    pimio::projection
    Qt6::Core
    Qt6::Gui
)
