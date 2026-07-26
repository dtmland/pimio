qt_add_executable(tst_browser_model
    tst_browser_model.cpp
)

target_link_libraries(tst_browser_model PRIVATE
    pimio::browser
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_test(NAME browser.model COMMAND tst_browser_model)
