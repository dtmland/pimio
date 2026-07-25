add_library(pimio_core STATIC
    include/pimio/core/version.h
    src/version.cpp
)

add_library(pimio::core ALIAS pimio_core)

target_include_directories(pimio_core PUBLIC include)

target_link_libraries(pimio_core PUBLIC Qt6::Core)

target_compile_definitions(pimio_core PRIVATE
    PIMIO_VERSION_STRING="${PROJECT_VERSION}"
)
