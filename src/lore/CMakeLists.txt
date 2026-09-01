# LORE-backed durable store.
#
# The library is loaded dynamically, so this target links Qt only. LORE's
# header is private: nothing outside this directory sees a LORE type, which
# keeps a version bump contained. See docs/decisions/0001-lore-durable-store.md.

add_library(pimio_lore STATIC
    include/pimio/lore/lore_durable_store.h
    src/lore_api.cpp
    src/lore_api.h
    src/lore_durable_store.cpp
    src/lore_durable_store_commit.cpp
    src/lore_durable_store_identity.cpp
    src/lore_durable_store_private.h
    src/lore_store_helpers.cpp
    src/lore_store_helpers.h
)

add_library(pimio::lore ALIAS pimio_lore)

target_include_directories(pimio_lore PUBLIC include)
target_include_directories(pimio_lore PRIVATE "${PIMIO_LORE_INCLUDE_DIR}")

target_link_libraries(pimio_lore PUBLIC pimio::core Qt6::Core)

target_compile_definitions(pimio_lore PRIVATE
    PIMIO_LORE_SHARED_LIBRARY="${PIMIO_LORE_SHARED_LIBRARY}"
    PIMIO_LORE_VERSION="${PIMIO_LORE_VERSION}"
)
