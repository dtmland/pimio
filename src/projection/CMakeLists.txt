add_library(pimio_projection STATIC
    include/pimio/projection/job_dispatcher.h
    include/pimio/projection/job_queue.h
    include/pimio/projection/migration.h
    include/pimio/projection/projection_database.h
    src/job_dispatcher.cpp
    src/job_queue.cpp
    src/job_queue_operations.cpp
    src/job_queue_private.h
    src/migration.cpp
    src/projection_database.cpp
    src/projection_db_private.h
    src/projection_db_queries.cpp
    src/projection_db_write.cpp
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
