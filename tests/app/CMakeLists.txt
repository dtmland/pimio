qt_add_executable(tst_app_smoke
    tst_app_smoke.cpp
)

target_link_libraries(tst_app_smoke PRIVATE
    pimio::app_lib
    pimio_app_libplugin
    Qt6::Test
)

# The QPA platform is chosen by the CTest preset so the same test can run
# offscreen everywhere and under Xvfb/X11 on Linux.
add_test(NAME app.smoke COMMAND tst_app_smoke)
