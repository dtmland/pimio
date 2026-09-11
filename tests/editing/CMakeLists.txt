add_executable(tst_image_editing tst_image_editing.cpp)
target_link_libraries(tst_image_editing PRIVATE pimio::editing pimio::core Qt6::Test)
target_compile_definitions(tst_image_editing PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data")
add_test(NAME editing.image_recipes COMMAND tst_image_editing)

add_executable(tst_metadata_edit_service tst_metadata_edit_service.cpp)
target_link_libraries(tst_metadata_edit_service PRIVATE
    pimio::editing pimio::testing pimio::core Qt6::Test)
target_compile_definitions(tst_metadata_edit_service PRIVATE
    PIMIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/data")
add_test(NAME editing.metadata_save COMMAND tst_metadata_edit_service)
