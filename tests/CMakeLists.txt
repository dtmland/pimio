add_subdirectory(support)
add_subdirectory(core)
add_subdirectory(settings)
add_subdirectory(fixtures)
add_subdirectory(metadata)
add_subdirectory(editing)
add_subdirectory(projection)
add_subdirectory(scan)
add_subdirectory(watch)
add_subdirectory(thumbnail)
add_subdirectory(browser)
add_subdirectory(app)
add_subdirectory(studio)

if(PIMIO_LORE_FOUND)
    add_subdirectory(lore)
endif()

# Opt-in negative control. Enabling this proves that a failing test is actually
# reported by every platform job instead of being silently skipped.
if(PIMIO_ENABLE_FAILING_SELFTEST)
    add_test(NAME selftest.deliberate_failure
        COMMAND ${CMAKE_COMMAND} -E false
    )
endif()

# LORE 0.14 refuses every command when the user's global config.toml is
# invalid. Point each test at its own empty LORE global directory so a
# developer's personal LORE configuration cannot change or break the results.
function(pimio_isolate_lore_global_config directory)
    get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
    foreach(test IN LISTS tests)
        set_property(TEST "${test}" DIRECTORY "${directory}" APPEND PROPERTY ENVIRONMENT
            "LORE_GLOBAL_PATH=${CMAKE_BINARY_DIR}/test-lore-global/${test}")
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        pimio_isolate_lore_global_config("${subdirectory}")
    endforeach()
endfunction()
pimio_isolate_lore_global_config("${CMAKE_CURRENT_SOURCE_DIR}")
