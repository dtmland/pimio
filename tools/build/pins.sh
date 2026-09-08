#!/usr/bin/env bash

# Shared bootstrap inputs; usable before CMake, Python, or Qt is installed.
pimio_load_build_pins() {
    local repository_root=$1
    unset PIMIO_QT_VERSION PIMIO_QT_MODULES PIMIO_AQTINSTALL_VERSION
    # shellcheck source=tools/build/qt.env
    source "$repository_root/tools/build/qt.env" || return 1
    PIMIO_LORE_VERSION=$(sed -n 's/^set(PIMIO_LORE_VERSION "\([0-9.]*\)".*/\1/p' \
        "$repository_root/cmake/PimioLore.cmake") || return 1
    if [[ ! "${PIMIO_QT_VERSION:-}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ||
          ! "$PIMIO_LORE_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ||
          ! "${PIMIO_AQTINSTALL_VERSION:-}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ||
          ! "${PIMIO_QT_MODULES:-}" =~ ^[a-z0-9]+(\ [a-z0-9]+)*$ ]]; then
        echo "Invalid or missing shared build pins in $repository_root." >&2
        return 1
    fi
}

pimio_load_build_pins "$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)" \
    || return 1
