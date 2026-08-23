if(NOT DEFINED PIMIO_LIBDE265_CMAKE_FILE)
    message(FATAL_ERROR "PIMIO_LIBDE265_CMAKE_FILE is required")
endif()

file(READ "${PIMIO_LIBDE265_CMAKE_FILE}" contents)

# libaom and libde265 both define a global development-only target named
# "dist". Namespace libde265's target so the two pinned projects can coexist.
set(original "add_custom_target(dist")
set(replacement "add_custom_target(pimio_libde265_dist")

string(FIND "${contents}" "${original}" original_offset)
if(NOT original_offset EQUAL -1)
    string(REPLACE "${original}" "${replacement}" contents "${contents}")
    file(WRITE "${PIMIO_LIBDE265_CMAKE_FILE}" "${contents}")
else()
    string(FIND "${contents}" "${replacement}" replacement_offset)
    if(replacement_offset EQUAL -1)
        message(FATAL_ERROR
            "Pinned libde265 CMake file no longer contains the expected dist target")
    endif()
endif()
