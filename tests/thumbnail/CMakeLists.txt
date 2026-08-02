qt_add_executable(tst_thumbnail_cache
    tst_thumbnail_cache.cpp
)

target_link_libraries(tst_thumbnail_cache PRIVATE
    pimio::thumbnail
    Qt6::Test
)

add_test(NAME thumbnail.cache COMMAND tst_thumbnail_cache)

qt_add_executable(tst_thumbnail_service
    tst_thumbnail_service.cpp
)

target_link_libraries(tst_thumbnail_service PRIVATE
    pimio::thumbnail
    Qt6::Test
)

add_test(NAME thumbnail.service COMMAND tst_thumbnail_service)

qt_add_executable(tst_thumbnail_video
    tst_thumbnail_video.cpp
)

target_link_libraries(tst_thumbnail_video PRIVATE
    pimio::thumbnail
    Qt6::Test
)

target_compile_definitions(tst_thumbnail_video PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data"
)

add_test(NAME thumbnail.video COMMAND tst_thumbnail_video)
