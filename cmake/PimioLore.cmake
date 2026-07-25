# Reproducible acquisition of the pinned LORE release.
#
# LORE is consumed as a dynamically loaded C shared library. pimio never links
# against it, so this module only has to make three things available:
#
#   * the C header, which is a build-time include directory;
#   * the shared library, which is resolved at run time by path; and
#   * the `lore` command line tool, which tests use as an independent external
#     writer.
#
# The same mechanism runs locally and in CI. Artifacts are downloaded once into
# a version-keyed cache directory, verified against a recorded SHA-256, and
# extracted next to the archive. Nothing is committed to the repository.
#
# See docs/dependency-bom.md for the licence and redistribution record, and
# docs/decisions/0001-lore-durable-store.md for the decisions this supports.

set(PIMIO_LORE_VERSION "0.8.5" CACHE STRING
    "Pinned LORE release consumed by pimio. Changing this requires new checksums.")

set(PIMIO_LORE_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/lore" CACHE PATH
    "Directory holding downloaded LORE artifacts. Safe to delete; safe to cache in CI.")

set(PIMIO_LORE_BASE_URL "https://github.com/EpicGames/lore/releases/download" CACHE STRING
    "Base URL of the LORE release artifacts.")

option(PIMIO_LORE_DOWNLOAD
    "Download the pinned LORE artifacts when they are not already cached" ON)

# Recorded SHA-256 checksums for every artifact pimio consumes. An artifact
# that is not listed here is never used, so a silently replaced release cannot
# enter a build.
set(_pimio_lore_checksums_0.8.5
    "liblore|x86_64-unknown-linux-gnu|tar.gz|50cdb35a73d5d63250125be3017fafea46f152b7c44f992d1996ae11b654f5ec"
    "liblore|aarch64-unknown-linux-gnu-neoverse-512tvb|tar.gz|ca8d22f2b57571f72988041b60f7c1af95048d98f7071ded891fc5204c97b6ba"
    "liblore|aarch64-apple-darwin|tar.gz|9daced7c31bff24bd054264861bfa2628b2d157cf0273bc2fce1961d67e8294b"
    "liblore|x86_64-pc-windows-msvc|zip|4beb1500db6b3fde2f0107378ca61d609f3aa4c18c8adfe57bfe389d70155b81"
    "lore|x86_64-unknown-linux-gnu|tar.gz|3d58bd36caaec2e9916489ec7e4fc7195a858e51cb71a3b6e90d72adfe3062ff"
    "lore|aarch64-unknown-linux-gnu-neoverse-512tvb|tar.gz|c6fc47d0fa0706f8d979d039a665bc7fb5ed7a8a23e9e69abcd9cab052781134"
    "lore|aarch64-apple-darwin|tar.gz|fba4eafb123afe599b5d752121fb6a5da722f33b2a3da3390324b654d31dc74d"
    "lore|x86_64-pc-windows-msvc|zip|c213169d251b73feb3fdf1655b9b5e6717a6a862762825918cc318a570018ded"
)

# Maps the host to the LORE target triple. An unmapped host is not an error:
# the build stays green and the LORE-backed tests skip with a stated reason.
function(_pimio_lore_host_triple out_triple out_reason)
    set(${out_reason} "" PARENT_SCOPE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x64)$")
            set(${out_triple} "x86_64-pc-windows-msvc" PARENT_SCOPE)
            return()
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(${out_triple} "aarch64-apple-darwin" PARENT_SCOPE)
            return()
        endif()
        # LORE publishes no macOS x86-64 build. See docs/supported-platforms.md.
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
            set(${out_triple} "x86_64-unknown-linux-gnu" PARENT_SCOPE)
            return()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            set(${out_triple} "aarch64-unknown-linux-gnu-neoverse-512tvb" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_triple} "" PARENT_SCOPE)
    set(${out_reason}
        "LORE publishes no artifact for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}"
        PARENT_SCOPE)
endfunction()

function(_pimio_lore_checksum bundle triple extension out_sha)
    foreach(entry IN LISTS _pimio_lore_checksums_${PIMIO_LORE_VERSION})
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 entry_bundle)
        list(GET fields 1 entry_triple)
        list(GET fields 2 entry_extension)
        list(GET fields 3 entry_sha)
        if(entry_bundle STREQUAL bundle
                AND entry_triple STREQUAL triple
                AND entry_extension STREQUAL extension)
            set(${out_sha} "${entry_sha}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_sha} "" PARENT_SCOPE)
endfunction()

# Downloads and extracts one artifact bundle. Sets <out_dir> to the extracted
# directory, or to an empty string when the bundle could not be made available.
function(_pimio_lore_acquire bundle triple out_dir out_reason)
    set(${out_dir} "" PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)

    if(triple MATCHES "windows")
        set(extension "zip")
    else()
        set(extension "tar.gz")
    endif()

    _pimio_lore_checksum("${bundle}" "${triple}" "${extension}" expected_sha)
    if(expected_sha STREQUAL "")
        set(${out_reason}
            "No recorded SHA-256 for ${bundle} ${PIMIO_LORE_VERSION} ${triple}"
            PARENT_SCOPE)
        return()
    endif()

    set(archive_name "${bundle}-v${PIMIO_LORE_VERSION}-${triple}.${extension}")
    # Version- and artifact-keyed so a version bump never reuses stale bytes.
    set(bundle_dir "${PIMIO_LORE_CACHE_DIR}/v${PIMIO_LORE_VERSION}/${triple}/${bundle}")
    set(archive_path "${bundle_dir}/${archive_name}")
    set(extract_dir "${bundle_dir}/extracted")
    set(stamp_path "${extract_dir}/.pimio-sha256")

    # A stamp carrying the verified checksum makes the cache self-validating: a
    # cache restored for a different version is re-acquired rather than used.
    if(EXISTS "${stamp_path}")
        file(READ "${stamp_path}" stamped_sha)
        string(STRIP "${stamped_sha}" stamped_sha)
        if(stamped_sha STREQUAL expected_sha)
            set(${out_dir} "${extract_dir}" PARENT_SCOPE)
            return()
        endif()
        file(REMOVE_RECURSE "${extract_dir}")
    endif()

    if(EXISTS "${archive_path}")
        file(SHA256 "${archive_path}" actual_sha)
        if(NOT actual_sha STREQUAL expected_sha)
            file(REMOVE "${archive_path}")
        endif()
    endif()

    if(NOT EXISTS "${archive_path}")
        if(NOT PIMIO_LORE_DOWNLOAD)
            set(${out_reason}
                "${archive_name} is not cached and PIMIO_LORE_DOWNLOAD is OFF"
                PARENT_SCOPE)
            return()
        endif()
        file(MAKE_DIRECTORY "${bundle_dir}")
        set(url "${PIMIO_LORE_BASE_URL}/v${PIMIO_LORE_VERSION}/${archive_name}")
        message(STATUS "Downloading ${archive_name}")
        file(DOWNLOAD "${url}" "${archive_path}"
            EXPECTED_HASH "SHA256=${expected_sha}"
            TLS_VERIFY ON
            INACTIVITY_TIMEOUT 60
            STATUS download_status
        )
        list(GET download_status 0 download_code)
        if(NOT download_code EQUAL 0)
            list(GET download_status 1 download_message)
            file(REMOVE "${archive_path}")
            set(${out_reason} "Failed to download ${url}: ${download_message}" PARENT_SCOPE)
            return()
        endif()
    endif()

    file(REMOVE_RECURSE "${extract_dir}")
    file(MAKE_DIRECTORY "${extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${extract_dir}")
    file(WRITE "${stamp_path}" "${expected_sha}")

    set(${out_dir} "${extract_dir}" PARENT_SCOPE)
endfunction()

# Entry point. Defines the variables consumed by src/lore and tests/lore:
#
#   PIMIO_LORE_FOUND               TRUE when the header and library both exist
#   PIMIO_LORE_INCLUDE_DIR         directory holding lore.h
#   PIMIO_LORE_SHARED_LIBRARY      liblore.so / liblore.dylib / lore.dll
#   PIMIO_LORE_CLI                 the lore CLI, or empty when unavailable
#   PIMIO_LORE_UNAVAILABLE_REASON  human-readable reason when not found
function(pimio_acquire_lore)
    set(PIMIO_LORE_FOUND FALSE PARENT_SCOPE)
    set(PIMIO_LORE_INCLUDE_DIR "" PARENT_SCOPE)
    set(PIMIO_LORE_SHARED_LIBRARY "" PARENT_SCOPE)
    set(PIMIO_LORE_CLI "" PARENT_SCOPE)

    if(NOT PIMIO_WITH_LORE)
        set(PIMIO_LORE_UNAVAILABLE_REASON "PIMIO_WITH_LORE is OFF" PARENT_SCOPE)
        return()
    endif()

    _pimio_lore_host_triple(triple reason)
    if(triple STREQUAL "")
        set(PIMIO_LORE_UNAVAILABLE_REASON "${reason}" PARENT_SCOPE)
        return()
    endif()

    _pimio_lore_acquire("liblore" "${triple}" library_dir library_reason)
    if(library_dir STREQUAL "")
        set(PIMIO_LORE_UNAVAILABLE_REASON "${library_reason}" PARENT_SCOPE)
        return()
    endif()

    if(WIN32)
        set(runtime_names "lore.dll")
    elseif(APPLE)
        set(runtime_names "liblore.dylib")
    else()
        set(runtime_names "liblore.so")
    endif()

    set(header_path "${library_dir}/lore.h")
    if(NOT EXISTS "${header_path}")
        set(PIMIO_LORE_UNAVAILABLE_REASON
            "No lore.h in the extracted bundle ${library_dir}" PARENT_SCOPE)
        return()
    endif()

    set(runtime_path "")
    foreach(name IN LISTS runtime_names)
        if(EXISTS "${library_dir}/${name}")
            set(runtime_path "${library_dir}/${name}")
            break()
        endif()
    endforeach()
    if(runtime_path STREQUAL "")
        set(PIMIO_LORE_UNAVAILABLE_REASON
            "No LORE shared library in the extracted bundle ${library_dir}" PARENT_SCOPE)
        return()
    endif()

    # The CLI is test-only. Its absence weakens the external-writer tests but
    # must not fail the build, so it is acquired separately and optionally.
    _pimio_lore_acquire("lore" "${triple}" cli_dir cli_reason)
    if(cli_dir STREQUAL "")
        message(STATUS "LORE CLI unavailable: ${cli_reason}")
    else()
        if(WIN32)
            set(cli_path "${cli_dir}/lore.exe")
        else()
            set(cli_path "${cli_dir}/lore")
        endif()
        if(EXISTS "${cli_path}")
            if(NOT WIN32)
                # Archive permissions are not always preserved by every
                # extractor, so make the tool executable explicitly.
                file(CHMOD "${cli_path}"
                    PERMISSIONS
                        OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE
                )
            endif()
            set(PIMIO_LORE_CLI "${cli_path}" PARENT_SCOPE)
        endif()
    endif()

    set(PIMIO_LORE_FOUND TRUE PARENT_SCOPE)
    set(PIMIO_LORE_INCLUDE_DIR "${library_dir}" PARENT_SCOPE)
    set(PIMIO_LORE_SHARED_LIBRARY "${runtime_path}" PARENT_SCOPE)
    set(PIMIO_LORE_UNAVAILABLE_REASON "" PARENT_SCOPE)
endfunction()
