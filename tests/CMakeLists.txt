add_subdirectory(support)
add_subdirectory(core)
add_subdirectory(fixtures)
add_subdirectory(metadata)
add_subdirectory(projection)
add_subdirectory(scan)
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
