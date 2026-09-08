add_executable(tst_image_editing tst_image_editing.cpp)
target_link_libraries(tst_image_editing PRIVATE pimio::editing pimio::core Qt6::Test)
target_compile_definitions(tst_image_editing PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data")
add_test(NAME editing.image_recipes COMMAND tst_image_editing)
