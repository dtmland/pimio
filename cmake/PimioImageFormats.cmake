include(FetchContent)

set(PIMIO_LIBAVIF_VERSION "1.4.2" CACHE STRING "libavif version used for AVIF decoding")
set(PIMIO_QT_AVIF_PLUGIN_VERSION "0.10.3" CACHE STRING
    "qt-avif-image-plugin version used for AVIF decoding")
set(PIMIO_LIBHEIF_VERSION "1.23.1" CACHE STRING "libheif version used for HEIC decoding")
set(PIMIO_LIBDE265_VERSION "1.1.1" CACHE STRING
    "libde265 version used for HEVC decoding")
set(PIMIO_QT_HEIC_PLUGIN_VERSION "0.7.1" CACHE STRING
    "qt-heic-image-plugin version used for HEIC decoding")

function(pimio_enable_image_formats)
    set(CMAKE_AUTOMOC OFF)
    set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(AVIF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(AVIF_CODEC_AOM LOCAL CACHE STRING "" FORCE)
    set(AVIF_CODEC_AOM_DECODE ON CACHE BOOL "" FORCE)
    set(AVIF_CODEC_AOM_ENCODE OFF CACHE BOOL "" FORCE)
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
        SOURCE_SUBDIR pimio-no-cmake-project
    )
    FetchContent_MakeAvailable(pimio_qt_avif_plugin_source)

    qt_add_plugin(pimio_avif_image_plugin STATIC
        CLASS_NAME QAVIFPlugin
        PLUGIN_TYPE imageformats)
    target_sources(pimio_avif_image_plugin PRIVATE
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/main.cpp"
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/qavifhandler.cpp"
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/qavifhandler_p.h"
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src/util_p.h"
    )
    target_include_directories(pimio_avif_image_plugin PRIVATE
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/src")
    target_link_libraries(pimio_avif_image_plugin PRIVATE Qt6::Gui avif)
    set_target_properties(pimio_avif_image_plugin PROPERTIES AUTOMOC ON)

    add_library(pimio::avif_image_plugin ALIAS pimio_avif_image_plugin)

    FetchContent_GetProperties(libaom)
    install(FILES
        "${pimio_libavif_SOURCE_DIR}/LICENSE"
        DESTINATION licenses
        RENAME libavif-LICENSE.txt)
    install(FILES
        "${pimio_qt_avif_plugin_source_SOURCE_DIR}/LICENSE"
        DESTINATION licenses
        RENAME qt-avif-image-plugin-LICENSE.txt)
    install(FILES
        "${libaom_SOURCE_DIR}/LICENSE"
        DESTINATION licenses
        RENAME libaom-LICENSE.txt)

    # Qt ImageFormats has no HEIC decoder. Qt Multimedia can expose an HEVC tile
    # as a video frame, but it does not compose HEIF grids, which makes many
    # phone photos look cropped. Build the maintained libheif-backed image
    # plugin instead. The LGPL components stay shared and separately replaceable.
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(ENABLE_SDL OFF CACHE BOOL "" FORCE)
    set(ENABLE_DECODER OFF CACHE BOOL "" FORCE)
    set(ENABLE_ENCODER OFF CACHE BOOL "" FORCE)
    set(ENABLE_SHERLOCK265 OFF CACHE BOOL "" FORCE)
    set(ENABLE_INTERNAL_DEVELOPMENT_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_FUZZERS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        pimio_libde265
        URL "https://github.com/strukturag/libde265/releases/download/v${PIMIO_LIBDE265_VERSION}/libde265-${PIMIO_LIBDE265_VERSION}.tar.gz"
        URL_HASH SHA256=fd48a927e94ed74fc7ce8829d222b9d8599fcbfe8b6448ba66705babc56ab219
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(pimio_libde265)

    # libde265's build target does not export its build-tree include paths, but
    # libheif needs both the source header and generated version header.
    target_include_directories(de265 PUBLIC
        "$<BUILD_INTERFACE:${pimio_libde265_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${pimio_libde265_BINARY_DIR}>")

    set(ENABLE_PLUGIN_LOADING OFF CACHE BOOL "" FORCE)
    set(ENABLE_MULTITHREADING_SUPPORT ON CACHE BOOL "" FORCE)
    set(ENABLE_PARALLEL_TILE_DECODING ON CACHE BOOL "" FORCE)
    set(WITH_LIBDE265 ON CACHE BOOL "" FORCE)
    set(WITH_LIBDE265_PLUGIN OFF CACHE BOOL "" FORCE)
    set(WITH_LIBSHARPYUV OFF CACHE BOOL "" FORCE)
    set(WITH_UNCOMPRESSED_CODEC OFF CACHE BOOL "" FORCE)
    set(WITH_HEADER_COMPRESSION OFF CACHE BOOL "" FORCE)
    set(WITH_WEBCODECS OFF CACHE BOOL "" FORCE)
    set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WITH_EXAMPLE_HEIF_THUMB OFF CACHE BOOL "" FORCE)
    set(WITH_EXAMPLE_HEIF_VIEW OFF CACHE BOOL "" FORCE)
    set(WITH_GDK_PIXBUF OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

    foreach(codec
            X265 KVAZAAR UVG266 VVDEC VVENC X264
            OpenH264_DECODER OpenH264_ENCODER
            DAV1D AOM_DECODER AOM_ENCODER SvtEnc RAV1E
            JPEG_DECODER JPEG_ENCODER
            OpenJPEG_ENCODER OpenJPEG_DECODER
            FFMPEG_DECODER OPENJPH_ENCODER OPENJPH_DECODER)
        set(WITH_${codec} OFF CACHE BOOL "" FORCE)
        set(WITH_${codec}_PLUGIN OFF CACHE BOOL "" FORCE)
    endforeach()

    # Let libheif's find module resolve the in-tree libde265 target instead of
    # accepting a different system installation on one build context.
    set(LIBDE265_INCLUDE_DIR "${pimio_libde265_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(LIBDE265_LIBRARY de265 CACHE STRING "" FORCE)

    FetchContent_Declare(
        pimio_libheif
        URL "https://github.com/strukturag/libheif/releases/download/v${PIMIO_LIBHEIF_VERSION}/libheif-${PIMIO_LIBHEIF_VERSION}.tar.gz"
        URL_HASH SHA256=0de0327f60fcd47de90d5654c6fe152232738d60d84fe084ec3e0f35e03b166a
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(pimio_libheif)

    # The 1.23.1 release tarball keeps public headers under libheif/api while
    # advertising the generated-source include layout used by its git build.
    target_include_directories(heif PUBLIC
        "$<BUILD_INTERFACE:${pimio_libheif_SOURCE_DIR}/libheif/api>"
        "$<BUILD_INTERFACE:${pimio_libheif_BINARY_DIR}>")

    FetchContent_Declare(
        pimio_qt_heic_plugin_source
        URL "https://github.com/novomesk/qt-heic-image-plugin/archive/refs/tags/v${PIMIO_QT_HEIC_PLUGIN_VERSION}.tar.gz"
        URL_HASH SHA256=48d39e60cd2f88e24612f6743ee029e5c794c9e826dee244d0b70a93e440fb1d
        SOURCE_SUBDIR pimio-no-cmake-project
    )
    FetchContent_MakeAvailable(pimio_qt_heic_plugin_source)

    qt_add_plugin(pimio_heic_image_plugin SHARED
        CLASS_NAME HEIFPlugin
        PLUGIN_TYPE imageformats)
    target_sources(pimio_heic_image_plugin PRIVATE
        "${pimio_qt_heic_plugin_source_SOURCE_DIR}/src/heif.cpp"
        "${pimio_qt_heic_plugin_source_SOURCE_DIR}/src/heif_p.h"
        "${pimio_qt_heic_plugin_source_SOURCE_DIR}/src/heif.json"
        "${pimio_qt_heic_plugin_source_SOURCE_DIR}/src/util_p.h")
    target_include_directories(pimio_heic_image_plugin PRIVATE
        "${pimio_qt_heic_plugin_source_SOURCE_DIR}/src")
    target_link_libraries(pimio_heic_image_plugin PRIVATE Qt6::Gui heif)
    set_target_properties(pimio_heic_image_plugin PROPERTIES
        AUTOMOC ON
        OUTPUT_NAME qheif
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/imageformats"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/imageformats")
    add_library(pimio::heic_image_plugin ALIAS pimio_heic_image_plugin)

    if(WIN32)
        set(PIMIO_HEIC_LIBRARY_DESTINATION bin)
        set(PIMIO_HEIC_PLUGIN_DESTINATION plugins/imageformats)
    elseif(APPLE)
        set(PIMIO_HEIC_LIBRARY_DESTINATION pimio.app/Contents/Frameworks)
        set(PIMIO_HEIC_PLUGIN_DESTINATION pimio.app/Contents/PlugIns/imageformats)
        set_target_properties(pimio_heic_image_plugin PROPERTIES
            INSTALL_RPATH "@loader_path/../../Frameworks")
        set_target_properties(heif de265 PROPERTIES INSTALL_RPATH "@loader_path")
    else()
        set(PIMIO_HEIC_LIBRARY_DESTINATION lib)
        set(PIMIO_HEIC_PLUGIN_DESTINATION plugins/imageformats)
        set_target_properties(pimio_heic_image_plugin PROPERTIES
            INSTALL_RPATH "$ORIGIN/../../lib")
        set_target_properties(heif de265 PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()

    install(TARGETS pimio_heic_image_plugin
        RUNTIME DESTINATION ${PIMIO_HEIC_PLUGIN_DESTINATION}
        LIBRARY DESTINATION ${PIMIO_HEIC_PLUGIN_DESTINATION})
    install(TARGETS heif de265
        RUNTIME DESTINATION ${PIMIO_HEIC_LIBRARY_DESTINATION}
        LIBRARY DESTINATION ${PIMIO_HEIC_LIBRARY_DESTINATION})
    install(FILES
        "${pimio_libheif_SOURCE_DIR}/COPYING"
        DESTINATION licenses
        RENAME libheif-COPYING.txt)
    install(FILES
        "${pimio_libde265_SOURCE_DIR}/COPYING"
        DESTINATION licenses
        RENAME libde265-COPYING.txt)
    install(FILES
        "${pimio_qt_heic_plugin_source_SOURCE_DIR}/LICENSE"
        DESTINATION licenses
        RENAME qt-heic-image-plugin-LICENSE.txt)

    # Keep the existing default for the rest of pimio's explicitly static
    # internal libraries and its permissively licensed AVIF dependencies.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endfunction()
