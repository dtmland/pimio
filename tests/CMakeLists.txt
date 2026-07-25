add_subdirectory(core)
add_subdirectory(app)

# Opt-in negative control. Enabling this proves that a failing test is actually
# reported by every platform job instead of being silently skipped.
if(PIMIO_ENABLE_FAILING_SELFTEST)
    add_test(NAME selftest.deliberate_failure
        COMMAND ${CMAKE_COMMAND} -E false
    )
endif()
