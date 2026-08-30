qt_add_executable(pimio_projection_migration_helper
    migration_helper_main.cpp
)

target_link_libraries(pimio_projection_migration_helper PRIVATE
    pimio::projection
    Qt6::Sql
)

qt_add_executable(tst_projection_migrations
    tst_projection_migrations.cpp
)

target_link_libraries(tst_projection_migrations PRIVATE
    pimio::projection
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_dependencies(tst_projection_migrations pimio_projection_migration_helper)

add_test(NAME projection.migrations COMMAND tst_projection_migrations)

qt_add_executable(tst_projection_rebuild
    tst_projection_rebuild.cpp
)

target_link_libraries(tst_projection_rebuild PRIVATE
    pimio::projection
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_test(NAME projection.rebuild COMMAND tst_projection_rebuild)

qt_add_executable(tst_projection_jobs
    tst_projection_jobs.cpp
)

target_link_libraries(tst_projection_jobs PRIVATE
    pimio::projection
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_test(NAME projection.jobs COMMAND tst_projection_jobs)

qt_add_executable(tst_projection_metadata
    tst_projection_metadata.cpp
    tst_projection_metadata_ordering.cpp
    tst_projection_metadata_search.cpp
)

target_link_libraries(tst_projection_metadata PRIVATE
    pimio::projection
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_test(NAME projection.metadata COMMAND tst_projection_metadata)

qt_add_executable(tst_projection_sort
    tst_projection_sort.cpp
)

target_link_libraries(tst_projection_sort PRIVATE
    pimio::projection
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_test(NAME projection.sort COMMAND tst_projection_sort)
