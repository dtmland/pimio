add_executable(tst_scan_incremental
    tst_scan_incremental.cpp
    tst_scan_incremental_hasher.cpp
    tst_scan_incremental_crud.cpp
)

target_link_libraries(tst_scan_incremental
    PRIVATE
        pimio::scan
        pimio::core
        pimio::test_support
        Qt6::Test
)

add_test(NAME scan.incremental COMMAND tst_scan_incremental)

qt_add_executable(tst_scan_qt_file_system tst_scan_qt_file_system.cpp)

target_link_libraries(tst_scan_qt_file_system
    PRIVATE
        pimio::scan
        pimio::core
        Qt6::Test
)

add_test(NAME scan.qt_file_system COMMAND tst_scan_qt_file_system)
