qt_add_executable(tst_fixture_manifest
    tst_fixture_manifest.cpp
)

target_link_libraries(tst_fixture_manifest PRIVATE
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(tst_fixture_manifest PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_test(NAME fixtures.manifest COMMAND tst_fixture_manifest)
