include(FetchContent)

set(PIMIO_EXIFTOOL_VERSION "13.59" CACHE STRING
    "ExifTool version used for embedded metadata writes")
set(PIMIO_EXIFTOOL_SHA256
    "87d3317882fdae9cb4dcfe57a96a378d0132ffc02c731315bf128b19ddcf7aac"
    CACHE STRING "SHA-256 of the pinned ExifTool source archive")

function(pimio_acquire_exiftool)
    find_package(Perl REQUIRED)

    FetchContent_Declare(
        pimio_exiftool
        URL "https://github.com/exiftool/exiftool/archive/refs/tags/${PIMIO_EXIFTOOL_VERSION}.tar.gz"
        URL_HASH "SHA256=${PIMIO_EXIFTOOL_SHA256}"
        SOURCE_SUBDIR pimio-no-cmake-project
    )
    FetchContent_MakeAvailable(pimio_exiftool)

    set(PIMIO_EXIFTOOL_SCRIPT "${pimio_exiftool_SOURCE_DIR}/exiftool"
        CACHE INTERNAL "Path to the pinned ExifTool script")
    set(PIMIO_EXIFTOOL_PERL "${PERL_EXECUTABLE}"
        CACHE INTERNAL "Perl interpreter used to run ExifTool")

    if(APPLE)
        set(_exiftool_destination "pimio.app/Contents/Resources/exiftool")
    else()
        set(_exiftool_destination "share/pimio/exiftool")
    endif()
    install(PROGRAMS "${pimio_exiftool_SOURCE_DIR}/exiftool"
        DESTINATION "${_exiftool_destination}")
    install(DIRECTORY "${pimio_exiftool_SOURCE_DIR}/lib/"
        DESTINATION "${_exiftool_destination}/lib")
    install(FILES "${pimio_exiftool_SOURCE_DIR}/LICENSE"
        DESTINATION licenses RENAME ExifTool-LICENSE.txt)

    if(WIN32)
        get_filename_component(_perl_bin "${PERL_EXECUTABLE}" DIRECTORY)
        get_filename_component(_perl_root "${_perl_bin}" DIRECTORY)
        file(GLOB _perl_runtime_dlls "${_perl_bin}/*.dll")
        install(PROGRAMS "${PERL_EXECUTABLE}" DESTINATION perl/bin)
        install(FILES ${_perl_runtime_dlls} DESTINATION perl/bin)
        install(DIRECTORY "${_perl_root}/lib/" DESTINATION perl/lib)
    endif()
endfunction()
