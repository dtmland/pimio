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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(PIMIO_BUILD_TESTS "Build pimio tests" ON)
option(PIMIO_ENABLE_FAILING_SELFTEST
    "Register a deliberately failing test that proves CI reports failures" OFF)

# LORE is the candidate durable store evaluated by Increment 2. It is optional
# at configure time so a contributor without the artifact still gets a green
# build; the LORE-backed tests then skip with a stated reason. CI enables it on
# every platform job, where a skip would hide the increment's evidence.
option(PIMIO_WITH_LORE "Acquire LORE and build the LORE-backed durable store" ON)
option(PIMIO_REQUIRE_LORE
    "Fail configuration when LORE is enabled but cannot be acquired" OFF)

# The supported Qt baseline is recorded in docs/supported-platforms.md.
set(PIMIO_QT_MINIMUM_VERSION 6.4)

find_package(Qt6 ${PIMIO_QT_MINIMUM_VERSION} REQUIRED COMPONENTS Core Gui Quick Sql)

qt_standard_project_setup(REQUIRES ${PIMIO_QT_MINIMUM_VERSION})

include(PimioLore)
pimio_acquire_lore()
if(PIMIO_LORE_FOUND)
    message(STATUS "LORE ${PIMIO_LORE_VERSION}: ${PIMIO_LORE_SHARED_LIBRARY}")
    if(PIMIO_LORE_CLI)
        message(STATUS "LORE CLI ${PIMIO_LORE_VERSION}: ${PIMIO_LORE_CLI}")
    endif()
elseif(PIMIO_REQUIRE_LORE)
    message(FATAL_ERROR "LORE is required but unavailable: ${PIMIO_LORE_UNAVAILABLE_REASON}")
else()
    message(STATUS "LORE unavailable: ${PIMIO_LORE_UNAVAILABLE_REASON}")
endif()

# A machine-readable record of what was actually acquired, so a CI job can
# report the version it used without re-deriving it.
if(PIMIO_LORE_FOUND)
    file(WRITE "${CMAKE_BINARY_DIR}/lore-acquired.txt"
        "version=${PIMIO_LORE_VERSION}\nlibrary=${PIMIO_LORE_SHARED_LIBRARY}\ncli=${PIMIO_LORE_CLI}\n")
else()
    file(WRITE "${CMAKE_BINARY_DIR}/lore-acquired.txt"
        "version=none\nreason=${PIMIO_LORE_UNAVAILABLE_REASON}\n")
endif()

add_subdirectory(src/core)
if(PIMIO_LORE_FOUND)
    add_subdirectory(src/lore)
endif()
add_subdirectory(src/metadata)
add_subdirectory(src/projection)
add_subdirectory(src/scan)
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
