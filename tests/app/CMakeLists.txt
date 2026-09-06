qt_add_executable(tst_app_smoke
    tst_app_smoke.cpp
    tst_app_smoke_fixture.h
    tst_app_smoke_navigation.cpp
    app_test_support.cpp
)

target_link_libraries(tst_app_smoke PRIVATE
    pimio::app_lib
    pimio_app_libplugin
    pimio::settings
    Qt6::Test
)

target_compile_definitions(tst_app_smoke PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data"
)

# The QPA platform is chosen by the CTest preset so the same test can run
# offscreen everywhere and under Xvfb/X11 on Linux.
add_test(NAME app.smoke COMMAND tst_app_smoke)

if(PIMIO_LORE_FOUND)
    qt_add_executable(tst_library_manager
        tst_library_manager.cpp
    )
    target_link_libraries(tst_library_manager PRIVATE
        pimio::app_lib
        pimio::lore
        pimio::projection
        Qt6::Test
    )
    add_test(NAME app.library_manager COMMAND tst_library_manager)
endif()
