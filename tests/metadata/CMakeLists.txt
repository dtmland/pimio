add_executable(tst_metadata_golden tst_metadata_golden.cpp)

target_link_libraries(tst_metadata_golden
    PRIVATE
        pimio::metadata
        pimio::scan
        pimio::core
        pimio::test_support
        Qt6::Test
)

target_compile_definitions(tst_metadata_golden PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data"
)

add_test(NAME metadata.golden COMMAND tst_metadata_golden)

add_executable(tst_metadata_write tst_metadata_write.cpp)
target_link_libraries(tst_metadata_write PRIVATE pimio::metadata pimio::core Qt6::Test)
target_compile_definitions(tst_metadata_write PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data")
add_test(NAME metadata.embedded_write COMMAND tst_metadata_write)
