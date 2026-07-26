qt_add_library(pimio_app_lib STATIC
    include/pimio/app/application.h
    src/application.cpp
)

add_library(pimio::app_lib ALIAS pimio_app_lib)

target_include_directories(pimio_app_lib PUBLIC include)

target_link_libraries(pimio_app_lib PUBLIC
    pimio::core
    pimio::browser
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
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

set_target_properties(pimio_app PROPERTIES
    OUTPUT_NAME pimio
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "pimio"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.pimio.pimio"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
)

target_link_libraries(pimio_app PRIVATE
    pimio::app_lib
    pimio_app_libplugin
)

# Generate a platform-aware Qt deployment script (windeployqt on Windows,
# macdeployqt on macOS, library copy on Linux).  The script is consumed by
# cmake --install in the release workflow; it has no effect during a plain
# build.
qt_generate_deploy_app_script(
    TARGET pimio_app
    FILENAME_VARIABLE PIMIO_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(TARGETS pimio_app
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)
install(SCRIPT ${PIMIO_DEPLOY_SCRIPT})
