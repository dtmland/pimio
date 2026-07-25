qt_add_executable(pimio_fixture_generator
    exif_builder.cpp
    exif_builder.h
    main.cpp
)

target_link_libraries(pimio_fixture_generator PRIVATE
    Qt6::Core
    Qt6::Gui
)
