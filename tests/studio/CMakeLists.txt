# Studio tests (Tests B): automated GUI tests that need a real display.
#
# They are excluded from the offscreen `default` test preset by the "studio"
# label and run through the `studio` test preset (native display) or the
# `default-x11` preset under Xvfb in CI. tools/field-tests/run-studio.{sh,ps1}
# run them on a real desktop and bundle the logs and screenshots they write.

qt_add_executable(tst_studio_empty_library
    tst_studio_empty_library.cpp
)

target_link_libraries(tst_studio_empty_library PRIVATE
    pimio::app_lib
    pimio_app_libplugin
    Qt6::Test
)

add_test(NAME studio.empty_library COMMAND tst_studio_empty_library)
set_tests_properties(studio.empty_library PROPERTIES LABELS "studio")
