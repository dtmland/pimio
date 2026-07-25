add_library(pimio_core STATIC
    include/pimio/core/clock.h
    include/pimio/core/durable_store.h
    include/pimio/core/edit_recipe.h
    include/pimio/core/error.h
    include/pimio/core/file_system.h
    include/pimio/core/geolocation.h
    include/pimio/core/job.h
    include/pimio/core/media_request.h
    include/pimio/core/metadata.h
    include/pimio/core/metadata_reader.h
    include/pimio/core/serialization.h
    include/pimio/core/types.h
    include/pimio/core/version.h
    src/boundaries.cpp
    src/clock.cpp
    src/durable_store.cpp
    src/edit_recipe.cpp
    src/error.cpp
    src/geolocation.cpp
    src/job.cpp
    src/media_request.cpp
    src/metadata.cpp
    src/serialization.cpp
    src/types.cpp
    src/version.cpp
)

add_library(pimio::core ALIAS pimio_core)

target_include_directories(pimio_core PUBLIC include)

# The core is deliberately UI-independent: it must never link Qt Gui, Qt Quick,
# or any other UI module. See docs/conventions.md.
target_link_libraries(pimio_core PUBLIC Qt6::Core)

target_compile_definitions(pimio_core PRIVATE
    PIMIO_VERSION_STRING="${PROJECT_VERSION}"
)
