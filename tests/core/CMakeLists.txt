qt_add_executable(tst_core_version
    tst_core_version.cpp
)

target_link_libraries(tst_core_version PRIVATE
    pimio::core
    Qt6::Test
)

add_test(NAME core.version COMMAND tst_core_version)
