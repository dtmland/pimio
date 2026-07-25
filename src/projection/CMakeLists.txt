add_library(pimio_projection STATIC
    include/pimio/projection/migration.h
    include/pimio/projection/projection_database.h
    src/migration.cpp
    src/projection_database.cpp
    src/projection_schema.cpp
)

add_library(pimio::projection ALIAS pimio_projection)

target_include_directories(pimio_projection PUBLIC include)

# Qt Sql lives here and nowhere else. pimio::core stays Qt6::Core-only so the
# durable store never gains a dependency on the disposable cache.
target_link_libraries(pimio_projection
    PUBLIC
        pimio::core
    PRIVATE
        Qt6::Sql
)
