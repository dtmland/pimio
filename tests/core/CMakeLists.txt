qt_add_executable(tst_core_version
    tst_core_version.cpp
)

target_link_libraries(tst_core_version PRIVATE
    pimio::core
    Qt6::Test
)

add_test(NAME core.version COMMAND tst_core_version)

qt_add_executable(tst_core_serialization
    tst_core_serialization.cpp
)

target_link_libraries(tst_core_serialization PRIVATE
    pimio::core
    pimio::test_support
    Qt6::Test
)

add_test(NAME core.serialization COMMAND tst_core_serialization)

qt_add_executable(tst_core_contracts
    tst_core_contracts.cpp
)

target_link_libraries(tst_core_contracts PRIVATE
    pimio::core
    pimio::test_support
    Qt6::Test
)

add_test(NAME core.contracts COMMAND tst_core_contracts)
