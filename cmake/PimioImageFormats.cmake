include(FetchContent)

set(PIMIO_LIBAVIF_VERSION "1.4.2" CACHE STRING "libavif version used for AVIF decoding")
set(PIMIO_QT_AVIF_PLUGIN_VERSION "0.10.3" CACHE STRING
    "qt-avif-image-plugin version used for AVIF decoding")

function(pimio_enable_image_formats)
    set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(AVIF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(AVIF_CODEC_AOM LOCAL CACHE STRING "" FORCE)
    set(AVIF_CODEC_AOM_DECODE ON CACHE BOOL "" FORCE)
    set(AVIF_CODEC_AOM_ENCODE ON CACHE BOOL "" FORCE)
    set(AVIF_LIBYUV OFF CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        pimio_libavif
        URL "https://github.com/AOMediaCodec/libavif/archive/refs/tags/v${PIMIO_LIBAVIF_VERSION}.tar.gz"
        URL_HASH SHA256=2b645287340ba5a631d268b551dc2d72bd73ac33335962dd36dcdb6d8366921d
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(pimio_libavif)

    FetchContent_Declare(
        pimio_qt_avif_plugin_source
        URL "https://github.com/novomesk/qt-avif-image-plugin/archive/refs/tags/v${PIMIO_QT_AVIF_PLUGIN_VERSION}.tar.gz"
        URL_HASH SHA256=89b254811d6117f9ab81146209224cb125c011fc0a7c6b139768527cc27748cd
    )
    FetchContent_GetProperties(pimio_qt_avif_plugin_source)
    if(NOT pimio_qt_avif_plugin_source_POPULATED)
        FetchContent_Populate(pimio_qt_avif_plugin_source)
    endif()

    qt_add_plugin(pimio_avif_image_plugin STATIC
        CLASS_NAME QAVIFPlugin
        PLUGIN_TYPE imageformats
        SOURCES
            "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/main.cpp"
            "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/qavifhandler.cpp"
            "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/qavifhandler_p.h"
            "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/util_p.h"
    )
    target_include_directories(pimio_avif_image_plugin PRIVATE
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src")
    target_link_libraries(pimio_avif_image_plugin PRIVATE Qt6::Gui avif)

    add_library(pimio::avif_image_plugin ALIAS pimio_avif_image_plugin)
endfunction()
