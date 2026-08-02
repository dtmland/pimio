#!/usr/bin/env bash
set -uo pipefail

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
# shellcheck source=/dev/null
source "$script_dir/pinned.sh"

usage() {
    cat <<'EOF'
Usage: tools/local-build/linux/build.sh [options]

Options:
  --output DIR       Results directory (default: build/local-build/linux/<timestamp>)
  --engine NAME      docker or podman (default: prefer podman)
  --image NAME       Container image name
  --pull             Pull --image instead of building the committed Containerfile
  --use-image        Use an existing --image without building or pulling it
  -h, --help         Show this help
EOF
}

copy_runtime_libraries() {
    local stage=$1 candidate ldd_output library
    local -a inputs=()

    while IFS= read -r -d '' candidate; do
        if file -b "$candidate" | grep -q '^ELF '; then
            inputs+=("$candidate")
        fi
    done < <(find "$stage/bin" "$stage/lib" "$stage/plugins" "$stage/qml" \
        -type f \( -executable -o -name '*.so*' \) -print0 2>/dev/null)

    mkdir -p "$stage/lib"
    for candidate in "${inputs[@]}"; do
        if ! ldd_output=$(ldd "$candidate" 2>&1); then
            printf '%s\n' "$ldd_output" >&2
            echo "Failed to inspect runtime dependencies for $candidate" >&2
            return 1
        fi
        while read -r library; do
            case "$(basename "$library")" in
                libEGL.so.1|libGLX.so.0|libGLdispatch.so.0|libOpenGL.so.0|\
                libX11-xcb.so.1|libX11.so.6|libXau.so.6|libXdmcp.so.6|\
                libxcb.so.1|libxcb-icccm.so.4|libxcb-image.so.0|libxcb-keysyms.so.1|\
                libxcb-randr.so.0|libxcb-render-util.so.0|libxcb-render.so.0|libxcb-shape.so.0|\
                libxcb-shm.so.0|libxcb-sync.so.1|libxcb-util.so.1|libxcb-xfixes.so.0|\
                libxkbcommon.so.0|libxkbcommon-x11.so.0|libxcb-cursor.so.0|\
                libxcb-glx.so.0|libxcb-present.so.0|libxcb-dri2.so.0|\
                libxcb-dri3.so.0|libxcb-xinerama.so.0|libxcb-xkb.so.1|\
                libwayland-client.so.0|libwayland-cursor.so.0|libwayland-egl.so.1)
                    local destination="$stage/lib/$(basename "$library")"
                    if [[ -e "$destination" && "$library" -ef "$destination" ]]; then
                        continue
                    fi
                    cp -L "$library" "$stage/lib/"
                    ;;
            esac
        done < <(printf '%s\n' "$ldd_output" | sed -n 's/.*=> \(\/[^ ]*\) (.*/\1/p' | sort -u)
    done
}

run_inside_container() (
    local results=$1
    local work=/work/source
    local stage=$work/stage
    local darkroom='not run' staging='not run' outcome=1
    local archive="$results/pimio-linux-x64.tar.gz"
    local started
    started=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    mkdir -p "$results" "$work"
    rm -rf "$results/Testing"
    rm -f "$results/build.log" "$results/darkroom-junit.xml" \
        "$results/environment.txt" "$archive" "$archive.tmp"
    exec > >(tee -a "$results/build.log") 2>&1

    # shellcheck disable=SC2317
    finish() {
        local finished commit
        finished=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        commit=${PIMIO_SOURCE_COMMIT:-unknown}
        {
            echo "pimio local build (Linux container)"
            echo "started : $started"
            echo "finished: $finished"
            echo "commit  : $commit"
            echo "image   : ${PIMIO_IMAGE_NAME:-unknown}"
            echo "image id: ${PIMIO_IMAGE_ID:-unknown}"
            echo "ubuntu  : $PIMIO_UBUNTU_VERSION (amd64)"
            echo "qt      : $PIMIO_QT_VERSION $PIMIO_QT_ARCH"
            echo "lore    : $PIMIO_LORE_VERSION"
            echo "cmake   : $(cmake --version | head -n 1)"
            echo "ninja   : $(ninja --version)"
            echo "compiler: $(c++ --version | head -n 1)"
            echo "commands: cmake --preset default -DPIMIO_REQUIRE_LORE=ON; cmake --build --preset default; ctest --preset default; cmake --install build/default"
            echo "darkroom: $darkroom"
            echo "stage   : $staging"
        } > "$results/environment.txt"
    }
    trap finish EXIT

    echo "== Clean working copy =="
    tar -C /source --exclude=.git --exclude=.cache --exclude=build -cf - . |
        tar -C "$work" -xf - || return
    cd "$work" || return

    echo "== Configure =="
    if ! cmake --preset default -DPIMIO_REQUIRE_LORE=ON; then
        darkroom='skipped: configure failed'
        staging='skipped: configure failed'
        return 1
    fi

    echo "== Build =="
    if ! cmake --build --preset default; then
        darkroom='skipped: build failed'
        staging='skipped: build failed'
        return 1
    fi

    echo "== Darkroom (Tests A) =="
    if ctest --preset default --output-on-failure \
        --output-junit "$results/darkroom-junit.xml"; then
        darkroom=passed
    else
        darkroom=failed
        staging='skipped: Darkroom did not pass'
        cp -a build/default/Testing "$results/" 2>/dev/null || true
        return 1
    fi
    cp -a build/default/Testing "$results/" 2>/dev/null || true

    echo "== Stage application =="
    rm -rf "$stage"
    if ! cmake --install build/default --prefix "$stage"; then
        staging='failed: cmake --install'
        return 1
    fi

    local lore_library
    lore_library=$(sed -n 's/^library=//p' build/default/lore-acquired.txt)
    if [[ -z "$lore_library" || ! -f "$lore_library" ]]; then
        staging='failed: acquired LORE library was not found'
        return 1
    fi
    if ! cp "$lore_library" "$stage/bin/"; then
        staging='failed: could not copy the acquired LORE library'
        return 1
    fi
    if ! copy_runtime_libraries "$stage"; then
        staging='failed: runtime library collection'
        return 1
    fi

    if ! tar -C "$stage" -czf "$archive.tmp" . ||
        ! mv "$archive.tmp" "$archive"; then
        rm -f "$archive.tmp" "$archive"
        staging='failed: could not create the application archive'
        return 1
    fi
    staging="staged: $archive"
    outcome=0
    echo "Build package: $archive"
    return "$outcome"
)

if [[ ${1:-} == --inside-container ]]; then
    run_inside_container "${2:?missing container results directory}"
    exit $?
fi

repository_root=$(pimio_repository_root)
pimio_assert_pins_match_repository "$repository_root" || exit 1

output=
requested_engine=
image=$PIMIO_LOCAL_IMAGE
image_mode=build
while (($#)); do
    case "$1" in
        --output) output=${2:?--output needs a directory}; shift 2 ;;
        --engine) requested_engine=${2:?--engine needs docker or podman}; shift 2 ;;
        --image) image=${2:?--image needs a name}; shift 2 ;;
        --pull) image_mode=pull; shift ;;
        --use-image) image_mode=use; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$(uname -m)" in
    x86_64|amd64) ;;
    *) echo "The Linux local-build image currently supports x86-64 hosts only." >&2; exit 1 ;;
esac

engine=$(pimio_select_container_engine "$requested_engine") || exit 1
if [[ -z "$output" ]]; then
    output="$repository_root/build/local-build/linux/$(date -u +%Y%m%d-%H%M%S)"
elif [[ "$output" != /* ]]; then
    output="$PWD/$output"
fi
mkdir -p "$output"
output=$(CDPATH='' cd -- "$output" && pwd -P)

echo "== Container image ($engine) =="
pimio_prepare_image "$engine" "$image" "$image_mode" "$repository_root" || exit 1
image_id=$("$engine" image inspect --format '{{.Id}}' "$image")
source_commit=$(git -C "$repository_root" rev-parse HEAD 2>/dev/null || echo unknown)

echo "== Build in clean container =="
identity_args=(--user "$(id -u):$(id -g)")
if [[ "$engine" == podman &&
    $("$engine" info --format '{{.Host.Security.Rootless}}') == true ]]; then
    identity_args+=(--userns=keep-id)
elif [[ "$engine" == docker ]] &&
    "$engine" info --format '{{json .SecurityOptions}}' | grep -q rootless; then
    # Container root maps to the invoking host user in Docker's rootless user
    # namespace; a numeric host UID would map to a subordinate host UID.
    identity_args=(--user 0:0)
fi
"$engine" run --rm --platform linux/amd64 \
    "${identity_args[@]}" \
    --tmpfs "/work:rw,exec,mode=1777" \
    -e HOME=/tmp \
    -e "PIMIO_SOURCE_COMMIT=$source_commit" \
    -e "PIMIO_IMAGE_NAME=$image" \
    -e "PIMIO_IMAGE_ID=$image_id" \
    --mount "type=bind,src=$repository_root,dst=/source,readonly" \
    --mount "type=bind,src=$output,dst=/results" \
    "$image" /source/tools/local-build/linux/build.sh --inside-container /results
status=$?

cp "$repository_root/tools/local-build/linux/Containerfile" "$output/Containerfile"
echo "Results: $output"
exit "$status"
