qt_add_executable(tst_app_smoke
    tst_app_smoke.cpp
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
