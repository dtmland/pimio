#!/usr/bin/env bash

# shellcheck disable=SC2034
PIMIO_UBUNTU_VERSION=24.04
PIMIO_UBUNTU_AMD64_DIGEST=sha256:52df9b1ee71626e0088f7d400d5c6b5f7bb916f8f0c82b474289a4ece6cf3faf
PIMIO_QT_VERSION=6.8.3
PIMIO_QT_ARCH=linux_gcc_64
PIMIO_AQTINSTALL_VERSION=3.3.0
PIMIO_LORE_VERSION=0.8.5
PIMIO_LOCAL_IMAGE=pimio-local-build-linux:ubuntu-24.04-qt-6.8.3

pimio_repository_root() {
    CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P
}

pimio_assert_pins_match_repository() {
    local repository_root=$1
    local ci_qt lore_version

    ci_qt=$(sed -n 's/^[[:space:]]*PIMIO_QT_VERSION:[[:space:]]*\([0-9.]*\).*/\1/p' \
        "$repository_root/.github/workflows/ci.yml" | head -n 1)
    lore_version=$(sed -n 's/^set(PIMIO_LORE_VERSION "\([0-9.]*\)".*/\1/p' \
        "$repository_root/cmake/PimioLore.cmake" | head -n 1)

    if [[ "$ci_qt" != "$PIMIO_QT_VERSION" ]]; then
        echo "Qt pin drift: ci.yml pins ${ci_qt:-unknown}, pinned.sh pins $PIMIO_QT_VERSION." >&2
        return 1
    fi
    if [[ "$lore_version" != "$PIMIO_LORE_VERSION" ]]; then
        echo "LORE pin drift: PimioLore.cmake pins ${lore_version:-unknown}, pinned.sh pins $PIMIO_LORE_VERSION." >&2
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
