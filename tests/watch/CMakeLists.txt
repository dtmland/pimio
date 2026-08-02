qt_add_executable(tst_watch_reconciliation tst_watch_reconciliation.cpp)

target_link_libraries(tst_watch_reconciliation
    PRIVATE
        pimio::watch
        pimio::scan
        pimio::projection
        pimio::core
        pimio::test_support
        Qt6::Test
)

add_test(NAME watch.reconciliation COMMAND tst_watch_reconciliation)

qt_add_executable(tst_watch_contract tst_watch_contract.cpp)

target_link_libraries(tst_watch_contract
    PRIVATE
        pimio::watch
        pimio::core
        Qt6::Test
)

add_test(NAME watch.contract COMMAND tst_watch_contract)

qt_add_executable(tst_watch_native tst_watch_native.cpp)

target_link_libraries(tst_watch_native
    PRIVATE
        pimio::watch
        pimio::core
        Qt6::Test
)

# Native integration test: runs on every platform runner, since
# QtDirectoryWatchAdapter is built on QFileSystemWatcher rather than any
# platform-specific code pimio maintains itself.
add_test(NAME watch.native COMMAND tst_watch_native)
