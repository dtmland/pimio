add_library(pimio_test_support STATIC
    include/pimio/testing/fake_clock.h
    include/pimio/testing/fake_metadata_reader.h
    include/pimio/testing/memory_durable_store.h
    include/pimio/testing/memory_file_system.h
    include/pimio/testing/qtest_printers.h
    include/pimio/testing/recording_media_request_service.h
    src/fake_clock.cpp
    src/fake_metadata_reader.cpp
    src/memory_durable_store.cpp
    src/memory_file_system.cpp
    src/recording_media_request_service.cpp
)

add_library(pimio::test_support ALIAS pimio_test_support)

target_include_directories(pimio_test_support PUBLIC include)

target_link_libraries(pimio_test_support PUBLIC pimio::core Qt6::Test)
