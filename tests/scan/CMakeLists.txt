add_executable(tst_scan_incremental tst_scan_incremental.cpp)

target_link_libraries(tst_scan_incremental
    PRIVATE
        pimio::scan
        pimio::core
        pimio::test_support
        Qt6::Test
)

add_test(NAME scan.incremental COMMAND tst_scan_incremental)
