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
#
# The QML variant is required: pimio is a QML application, and the non-QML
# deployment script neither scans the QML imports nor installs the `qml/`
# import tree, so a deployed build would start and then fail with
# "module QtQuick is not installed".  DEPLOY_USER_QML_MODULES... makes the
# Linux path (which Qt treats as an unsupported deployment platform) also
# receive the project's own QML module.  That keyword only exists from
# Qt 6.5, and passing it to 6.4 is a hard configure error, so it is opt-in.
set(PIMIO_DEPLOY_EXTRA_ARGS "")
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    list(APPEND PIMIO_DEPLOY_EXTRA_ARGS
        DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM)
endif()

qt_generate_deploy_qml_app_script(
    TARGET pimio_app
    FILENAME_VARIABLE PIMIO_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${PIMIO_DEPLOY_EXTRA_ARGS}
)

install(TARGETS pimio_app
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)
install(SCRIPT ${PIMIO_DEPLOY_SCRIPT})

# Launcher, README, and diagnostic tool shipped at the root of every release
# archive.  They are installed rather than copied by the workflow so that a
# local `cmake --install` produces the same tree the release publishes.
if(WIN32)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/pimio.bat DESTINATION .)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/pimio-doctor.ps1 DESTINATION .)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/README-Windows.txt
        DESTINATION . RENAME README.txt)
else()
    if(APPLE)
        set(PIMIO_PACKAGE_README README-macOS.txt)
    else()
        set(PIMIO_PACKAGE_README README-Linux.txt)
    endif()
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/pimio.sh
        DESTINATION . RENAME pimio)
    install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/pimio-doctor.sh
        DESTINATION . RENAME pimio-doctor)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/${PIMIO_PACKAGE_README}
        DESTINATION . RENAME README.txt)
endif()
