cmake_minimum_required(VERSION 3.24)

project(pimio
    VERSION 0.1.0
    DESCRIPTION "Local-first photo and video organizer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(PIMIO_BUILD_TESTS "Build pimio tests" ON)
option(PIMIO_ENABLE_FAILING_SELFTEST
    "Register a deliberately failing test that proves CI reports failures" OFF)

# The supported Qt baseline is recorded in docs/supported-platforms.md.
set(PIMIO_QT_MINIMUM_VERSION 6.4)

find_package(Qt6 ${PIMIO_QT_MINIMUM_VERSION} REQUIRED COMPONENTS Core Gui Quick)

qt_standard_project_setup(REQUIRES ${PIMIO_QT_MINIMUM_VERSION})

add_subdirectory(src/core)
add_subdirectory(src/app)

if(PIMIO_BUILD_TESTS)
    # The fixture generator is a maintenance tool, not a shipped component.
    add_subdirectory(tools/fixture_generator)
endif()

if(PIMIO_BUILD_TESTS)
    find_package(Qt6 ${PIMIO_QT_MINIMUM_VERSION} REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
