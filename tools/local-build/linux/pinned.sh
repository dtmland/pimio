#!/usr/bin/env bash

# shellcheck disable=SC2034
PIMIO_UBUNTU_VERSION=24.04
PIMIO_UBUNTU_AMD64_DIGEST=sha256:52df9b1ee71626e0088f7d400d5c6b5f7bb916f8f0c82b474289a4ece6cf3faf
PIMIO_QT_VERSION=6.8.3
PIMIO_QT_ARCH=linux_gcc_64
# Must match the modules installed by .github/workflows/ci.yml. pimio links
# Qt6::Multimedia (see src/thumbnail/CMakeLists.txt), so the Qt base package is
# not enough; without these add-ons configuration fails with "Failed to find
# required Qt component Multimedia".
PIMIO_QT_MODULES="qtmultimedia qtimageformats"
PIMIO_AQTINSTALL_VERSION=3.3.0
PIMIO_LORE_VERSION=0.9.0
PIMIO_LOCAL_IMAGE=pimio-local-build-linux:ubuntu-24.04-qt-6.8.3

pimio_repository_root() {
    CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P
}

pimio_assert_pins_match_repository() {
    local repository_root=$1
    local ci_qt ci_modules lore_version

    ci_qt=$(sed -n 's/^[[:space:]]*PIMIO_QT_VERSION:[[:space:]]*\([0-9.]*\).*/\1/p' \
        "$repository_root/.github/workflows/ci.yml" | head -n 1)
    ci_modules=$(sed -n 's/^[[:space:]]*modules:[[:space:]]*\(.*\)/\1/p' \
        "$repository_root/.github/workflows/ci.yml" | head -n 1)
    lore_version=$(sed -n 's/^set(PIMIO_LORE_VERSION "\([0-9.]*\)".*/\1/p' \
        "$repository_root/cmake/PimioLore.cmake" | head -n 1)

    if [[ "$ci_qt" != "$PIMIO_QT_VERSION" ]]; then
        echo "Qt pin drift: ci.yml pins ${ci_qt:-unknown}, pinned.sh pins $PIMIO_QT_VERSION." >&2
        return 1
    fi
    if [[ -z "$ci_modules" ]]; then
        echo "Cannot find the Qt 'modules:' line in ci.yml." >&2
        return 1
    fi
    local ci_modules_sorted local_modules_sorted
    ci_modules_sorted=$(printf '%s\n' $ci_modules | sort | tr '\n' ' ')
    local_modules_sorted=$(printf '%s\n' $PIMIO_QT_MODULES | sort | tr '\n' ' ')
    if [[ "$ci_modules_sorted" != "$local_modules_sorted" ]]; then
        echo "Qt module pin drift: ci.yml installs '${ci_modules:-none}', pinned.sh pins '$PIMIO_QT_MODULES'." >&2
        return 1
    fi
    if [[ "$lore_version" != "$PIMIO_LORE_VERSION" ]]; then
        echo "LORE pin drift: PimioLore.cmake pins ${lore_version:-unknown}, pinned.sh pins $PIMIO_LORE_VERSION." >&2
        return 1
    fi

    # The release workflow provisions a fourth environment and must not drift
    # from ci.yml: a release built against a different Qt, module set, or LORE
    # than CI verified would ship untested bytes. See docs/build-architecture.md.
    local release_yml="$repository_root/.github/workflows/release.yml"
    local release_qt release_modules release_lore
    release_qt=$(sed -n 's/^[[:space:]]*PIMIO_QT_VERSION:[[:space:]]*\([0-9.]*\).*/\1/p' \
        "$release_yml" | head -n 1)
    release_modules=$(sed -n 's/^[[:space:]]*qt_modules:[[:space:]]*\(.*\)/\1/p' \
        "$release_yml" | head -n 1)
    release_lore=$(sed -n 's/^[[:space:]]*PIMIO_LORE_VERSION:[[:space:]]*\([0-9.]*\).*/\1/p' \
        "$release_yml" | head -n 1)

    if [[ "$release_qt" != "$ci_qt" ]]; then
        echo "Qt pin drift: release.yml pins ${release_qt:-unknown}, ci.yml pins $ci_qt." >&2
        return 1
    fi
    if [[ -z "$release_modules" ]]; then
        echo "Cannot find the Qt 'qt_modules:' line in release.yml." >&2
        return 1
    fi
    local release_modules_sorted
    release_modules_sorted=$(printf '%s\n' $release_modules | sort | tr '\n' ' ')
    if [[ "$release_modules_sorted" != "$ci_modules_sorted" ]]; then
        echo "Qt module pin drift: release.yml installs '${release_modules:-none}', ci.yml installs '${ci_modules:-none}'." >&2
        return 1
    fi
    if [[ "$release_lore" != "$lore_version" ]]; then
        echo "LORE pin drift: release.yml pins ${release_lore:-unknown}, PimioLore.cmake pins $lore_version." >&2
        return 1
    fi
}

pimio_select_container_engine() {
    local requested=${1:-}
    if [[ -n "$requested" ]]; then
        command -v "$requested" >/dev/null 2>&1 || {
            echo "Container engine '$requested' was not found on PATH." >&2
            return 1
        }
        printf '%s\n' "$requested"
        return
    fi
    if command -v podman >/dev/null 2>&1; then
        printf '%s\n' podman
    elif command -v docker >/dev/null 2>&1; then
        printf '%s\n' docker
    else
        echo "Neither Podman nor Docker was found on PATH." >&2
        return 1
    fi
}

pimio_prepare_image() {
    local engine=$1
    local image=$2
    local mode=$3
    local repository_root=$4

    case "$mode" in
        build)
            "$engine" build --platform linux/amd64 \
                --build-arg "UBUNTU_VERSION=$PIMIO_UBUNTU_VERSION" \
                --build-arg "UBUNTU_AMD64_DIGEST=$PIMIO_UBUNTU_AMD64_DIGEST" \
                --build-arg "QT_VERSION=$PIMIO_QT_VERSION" \
                --build-arg "QT_ARCH=$PIMIO_QT_ARCH" \
                --build-arg "QT_MODULES=$PIMIO_QT_MODULES" \
                --build-arg "AQTINSTALL_VERSION=$PIMIO_AQTINSTALL_VERSION" \
                --label "org.opencontainers.image.revision=$(git -C "$repository_root" rev-parse HEAD 2>/dev/null || echo unknown)" \
                -f "$repository_root/tools/local-build/linux/Containerfile" \
                -t "$image" "$repository_root/tools/local-build/linux"
            ;;
        pull)
            "$engine" pull "$image"
            ;;
        use)
            "$engine" image inspect "$image" >/dev/null
            ;;
        *)
            echo "Unknown image mode: $mode" >&2
            return 1
            ;;
    esac
}
