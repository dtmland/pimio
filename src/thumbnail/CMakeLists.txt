qt_add_library(pimio_thumbnail STATIC
    include/pimio/thumbnail/thumbnail_disk_cache.h
    include/pimio/thumbnail/thumbnail_renderer.h
    include/pimio/thumbnail/image_renderer.h
    include/pimio/thumbnail/video_frame_renderer.h
    include/pimio/thumbnail/composite_renderer.h
    include/pimio/thumbnail/thumbnail_service.h
    src/thumbnail_disk_cache.cpp
    src/thumbnail_renderer.cpp
    src/image_renderer.cpp
    src/video_frame_renderer.cpp
    src/composite_renderer.cpp
    src/thumbnail_service.cpp
)

add_library(pimio::thumbnail ALIAS pimio_thumbnail)

target_include_directories(pimio_thumbnail PUBLIC include)

# Qt Multimedia gives VideoFrameRenderer a genuine cross-platform decode path
# (Windows Media Foundation, GStreamer, AVFoundation) without pimio linking or
# shipping any codec library itself. See docs/dependency-bom.md.
target_link_libraries(pimio_thumbnail PUBLIC
    pimio::core
    pimio::avif_image_plugin
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
)
