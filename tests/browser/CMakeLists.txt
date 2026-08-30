qt_add_executable(tst_browser_model
    tst_browser_model.cpp
    tst_browser_model_fixture.h
    tst_browser_model_roles.cpp
    tst_browser_model_thumbnails.cpp
)

target_link_libraries(tst_browser_model PRIVATE
    pimio::browser
    pimio::test_support
    Qt6::Sql
    Qt6::Test
)

add_test(NAME browser.model COMMAND tst_browser_model)

qt_add_executable(tst_browser_thumbnail_image_provider
    tst_browser_thumbnail_image_provider.cpp
)

target_link_libraries(tst_browser_thumbnail_image_provider PRIVATE
    pimio::browser
    Qt6::Test
)

add_test(NAME browser.thumbnail_image_provider COMMAND tst_browser_thumbnail_image_provider)
