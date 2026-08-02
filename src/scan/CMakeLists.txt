add_library(pimio_scan STATIC
    include/pimio/scan/library_root.h
    include/pimio/scan/media_hasher.h
    include/pimio/scan/qt_file_system.h
    include/pimio/scan/scanner.h
    src/media_hasher.cpp
    src/qt_file_system.cpp
    src/scanner.cpp
)

add_library(pimio::scan ALIAS pimio_scan)

target_include_directories(pimio_scan PUBLIC include)

target_link_libraries(pimio_scan
    PUBLIC
        pimio::core
    PRIVATE
        Qt6::Core
)
