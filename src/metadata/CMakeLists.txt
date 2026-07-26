add_library(pimio_metadata STATIC
    include/pimio/metadata/builtin_metadata_reader.h
    src/builtin_metadata_reader.cpp
    src/field_set.h
    src/image_reader.cpp
    src/image_reader.h
    src/iso_bmff_reader.cpp
    src/iso_bmff_reader.h
    src/media_signature.cpp
    src/media_signature.h
    src/xmp_reader.cpp
    src/xmp_reader.h
)

add_library(pimio::metadata ALIAS pimio_metadata)

target_include_directories(pimio_metadata PUBLIC include)

target_link_libraries(pimio_metadata
    PUBLIC
        pimio::core
    PRIVATE
        Qt6::Core
)
