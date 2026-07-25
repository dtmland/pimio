qt_add_library(pimio_app_lib STATIC
    include/pimio/app/application.h
    src/application.cpp
)

add_library(pimio::app_lib ALIAS pimio_app_lib)

target_include_directories(pimio_app_lib PUBLIC include)

target_link_libraries(pimio_app_lib PUBLIC
    pimio::core
    Qt6::Gui
    Qt6::Quick
)

qt_add_qml_module(pimio_app_lib
    URI Pimio
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Pimio
    QML_FILES
        qml/Main.qml
)

qt_add_executable(pimio_app
    src/main.cpp
)

set_target_properties(pimio_app PROPERTIES OUTPUT_NAME pimio)

target_link_libraries(pimio_app PRIVATE
    pimio::app_lib
    pimio_app_libplugin
)
