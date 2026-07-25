# Increment 2 evidence: the LORE feasibility gate.
#
# These tests run against a real, local, offline LORE repository in a temporary
# directory. They skip with a stated reason when the artifact is unavailable so
# a contributor without it still gets a green run; CI enables LORE on every
# platform job, where a skip would hide the evidence.

qt_add_executable(tst_lore_adapter
    tst_lore_adapter.cpp
    lore_test_support.h
)

target_link_libraries(tst_lore_adapter PRIVATE
    pimio::core
    pimio::lore
    pimio::test_support
    Qt6::Test
)

target_compile_definitions(tst_lore_adapter PRIVATE
    PIMIO_LORE_CLI_PATH="${PIMIO_LORE_CLI}"
    PIMIO_LORE_EXPECTED_VERSION="${PIMIO_LORE_VERSION}"
)

add_test(NAME lore.adapter COMMAND tst_lore_adapter)

# Fault injection needs a process that can be killed outright, which cannot be
# done from inside the test executable without taking the test down with it.
qt_add_executable(pimio_lore_fault_helper
    fault_helper_main.cpp
)

target_link_libraries(pimio_lore_fault_helper PRIVATE
    pimio::core
    pimio::lore
)

qt_add_executable(tst_lore_faults
    tst_lore_faults.cpp
    lore_test_support.h
)

target_link_libraries(tst_lore_faults PRIVATE
    pimio::core
    pimio::lore
    pimio::test_support
    Qt6::Test
)

target_compile_definitions(tst_lore_faults PRIVATE
    PIMIO_LORE_CLI_PATH="${PIMIO_LORE_CLI}"
    PIMIO_LORE_EXPECTED_VERSION="${PIMIO_LORE_VERSION}"
)

add_test(NAME lore.faults COMMAND tst_lore_faults)

add_dependencies(tst_lore_faults pimio_lore_fault_helper)

# Fault injection restarts processes and walks large synthetic record sets, so
# it needs more headroom than the default CTest timeout.
set_tests_properties(lore.faults PROPERTIES TIMEOUT 900)
set_tests_properties(lore.adapter PROPERTIES TIMEOUT 600)
