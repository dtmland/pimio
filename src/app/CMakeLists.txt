qt_add_library(pimio_app_lib STATIC
    include/pimio/app/application.h
    include/pimio/app/library_activity.h
    include/pimio/app/library_manager.h
    include/pimio/app/library_session.h
    src/application.cpp
    src/library_activity.cpp
    src/library_manager.cpp
    src/library_manager_storage.cpp
    src/library_manager_storage.h
    src/library_session.cpp
)

add_library(pimio::app_lib ALIAS pimio_app_lib)

target_include_directories(pimio_app_lib PUBLIC include)

target_link_libraries(pimio_app_lib PUBLIC
    pimio::core
    pimio::browser
    pimio::metadata
    pimio::projection
    pimio::scan
    pimio::settings
    pimio::thumbnail
    pimio::watch
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)

# LibrarySession only opens a real durable store when this build was
# compiled with LORE; PIMIO_HAVE_LORE gates that code path so a build without
# LORE (see docs/decisions/0001-lore-durable-store.md) still links and starts
# with an empty library rather than failing to configure at all.
if(PIMIO_LORE_FOUND)
    target_link_libraries(pimio_app_lib PUBLIC pimio::lore)
    target_compile_definitions(pimio_app_lib PUBLIC PIMIO_HAVE_LORE)
endif()

qt_add_qml_module(pimio_app_lib
    URI Pimio
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Pimio
    QML_FILES
        qml/DetailView.qml
        qml/CameraIcon.qml
        qml/Main.qml
        qml/LibraryManagerDialog.qml
        qml/MediaTile.qml
        qml/PromotionDialog.qml
        qml/ScrollController.qml
        qml/SettingsDialog.qml
        qml/TopBar.qml
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

# Qt's deployment tools bundle only the desktop platform plugin: libqxcb.so on
# Linux, libqcocoa.dylib on macOS, qwindows.dll on Windows.  Anything headless
# -- a server, an SSH session, CI, and the QT_QPA_PLATFORM=offscreen fallback
# that README.txt and pimio-doctor both recommend -- then dies with "no Qt
# platform plugin could be initialized" (on Windows the resulting abort dialog
# simply hangs the process).  Ship the offscreen plugin alongside the desktop
# one so that a release archive can run without a display.
if(WIN32)
    set(PIMIO_OFFSCREEN_PLUGIN_NAME qoffscreen.dll)
    set(PIMIO_PLATFORM_PLUGIN_DESTINATION plugins/platforms)
elseif(APPLE)
    set(PIMIO_OFFSCREEN_PLUGIN_NAME libqoffscreen.dylib)
    set(PIMIO_PLATFORM_PLUGIN_DESTINATION pimio.app/Contents/PlugIns/platforms)
else()
    set(PIMIO_OFFSCREEN_PLUGIN_NAME libqoffscreen.so)
    set(PIMIO_PLATFORM_PLUGIN_DESTINATION plugins/platforms)
endif()

install(FILES
    "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms/${PIMIO_OFFSCREEN_PLUGIN_NAME}"
    DESTINATION ${PIMIO_PLATFORM_PLUGIN_DESTINATION})

if(APPLE)
    # macdeployqt never saw this plugin, so its Qt references are still the
    # @rpath ones of the Qt installation.  Point them at the frameworks inside
    # the bundle the way macdeployqt does for the plugins it copies, then
    # re-sign: an edited Mach-O carries a broken signature and macOS refuses to
    # load it on Apple silicon.
    install(CODE "
        set(_plugin \"\${CMAKE_INSTALL_PREFIX}/${PIMIO_PLATFORM_PLUGIN_DESTINATION}/${PIMIO_OFFSCREEN_PLUGIN_NAME}\")
        execute_process(COMMAND otool -L \"\${_plugin}\"
            OUTPUT_VARIABLE _otool_output
            RESULT_VARIABLE _otool_result)
        if(NOT _otool_result EQUAL 0)
            message(FATAL_ERROR \"otool failed for \${_plugin}\")
        endif()
        string(REGEX MATCHALL \"@rpath/[^ \\t\\r\\n]+\" _rpath_references \"\${_otool_output}\")
        list(REMOVE_DUPLICATES _rpath_references)
        foreach(_reference IN LISTS _rpath_references)
            string(REPLACE \"@rpath/\" \"@loader_path/../../Frameworks/\"
                _bundled \"\${_reference}\")
            execute_process(COMMAND install_name_tool
                -change \"\${_reference}\" \"\${_bundled}\" \"\${_plugin}\"
                COMMAND_ERROR_IS_FATAL ANY)
        endforeach()
        execute_process(COMMAND codesign --force --sign - \"\${_plugin}\"
            COMMAND_ERROR_IS_FATAL ANY)
    ")

    # macdeployqt seals the bundle when it finishes, so every file added or
    # edited afterwards -- the plugin above, and whatever the release workflow
    # copies into Contents/ -- leaves that seal stale.  macOS does not warn
    # about a stale seal: it kills the process as soon as a mapped page fails
    # validation ("Termination Reason: CODESIGNING, Invalid Page"), which for a
    # Qt application happens while QFactoryLoader scans the plugin directory,
    # before any window appears.  Re-sign the bundle as the last deployment
    # action and refuse to install one that does not verify.
    install(CODE "
        set(_bundle \"\${CMAKE_INSTALL_PREFIX}/pimio.app\")
        if(EXISTS \"\${_bundle}\")
            message(STATUS \"Ad-hoc signing \${_bundle}\")
            execute_process(COMMAND codesign --force --deep --sign - \"\${_bundle}\"
                COMMAND_ERROR_IS_FATAL ANY)
            execute_process(COMMAND codesign --verify --deep --strict \"\${_bundle}\"
                COMMAND_ERROR_IS_FATAL ANY)
        endif()
    ")
elseif(UNIX)
    # The Linux deployment relocates the Qt libraries into lib/ and patchelfs
    # every file it copied.  This plugin arrives afterwards, so give it the same
    # relative RPATH; the Qt installation's own value may be absolute.
    find_program(PIMIO_PATCHELF_EXECUTABLE patchelf)
    if(PIMIO_PATCHELF_EXECUTABLE)
        install(CODE "
            execute_process(COMMAND \"${PIMIO_PATCHELF_EXECUTABLE}\"
                --set-rpath \"\$ORIGIN/../../lib\"
                \"\${CMAKE_INSTALL_PREFIX}/${PIMIO_PLATFORM_PLUGIN_DESTINATION}/${PIMIO_OFFSCREEN_PLUGIN_NAME}\"
                COMMAND_ERROR_IS_FATAL ANY)
        ")
    endif()
endif()

# The Qt SDK ships a Mimer SQL driver plugin (libqsqlmimer.so) that has a hard
# dependency on libmimerapi.so.  That library is not available as a system
# package on any supported Linux distribution, and pimio uses SQLite rather
# than Mimer SQL, so the plugin is never loaded.  Remove it from Linux
# deployments so that the bundled ELF dependency check in the release workflow
# does not fail with "libmimerapi.so => not found".
if(UNIX AND NOT APPLE)
    install(CODE "
        set(_mimer \"\${CMAKE_INSTALL_PREFIX}/plugins/sqldrivers/libqsqlmimer.so\")
        if(EXISTS \"\${_mimer}\")
            file(REMOVE \"\${_mimer}\")
            message(STATUS \"Removing Mimer SQL plugin (libmimerapi.so unavailable): \${_mimer}\")
        endif()
    ")
endif()

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
