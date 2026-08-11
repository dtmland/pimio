qt_add_executable(tst_settings_store
    tst_settings_store.cpp
)

target_link_libraries(tst_settings_store PRIVATE
    pimio::settings
    Qt6::Test
)

add_test(NAME settings.store COMMAND tst_settings_store)
