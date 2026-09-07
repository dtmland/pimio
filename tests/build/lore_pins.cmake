cmake_minimum_required(VERSION 3.24)
include("${REPOSITORY_ROOT}/cmake/PimioLore.cmake")
_pimio_lore_checksum("liblore" "x86_64-pc-windows-msvc" "zip" library_sha)
_pimio_lore_checksum("lore" "x86_64-pc-windows-msvc" "zip" cli_sha)
file(WRITE "${OUTPUT_FILE}"
    "{\"LoreVersion\":\"${PIMIO_LORE_VERSION}\","
    "\"LoreBaseUrl\":\"${PIMIO_LORE_BASE_URL}\","
    "\"liblore\":\"${library_sha}\",\"lore\":\"${cli_sha}\"}")
