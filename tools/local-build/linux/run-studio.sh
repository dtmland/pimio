#!/usr/bin/env bash
set -uo pipefail

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
# shellcheck source=/dev/null
source "$script_dir/pinned.sh"

usage() {
    cat <<'EOF'
Usage: tools/local-build/linux/run-studio.sh [options]

Options:
  --display MODE     auto, x11, or wayland (default: auto)
  --output DIR       Results directory (default: build/local-build/linux/studio-<timestamp>)
  --engine NAME      docker or podman (default: prefer podman)
  --image NAME       Container image name
  --pull             Pull --image instead of building the committed Containerfile
  --use-image        Use an existing --image without building or pulling it
  -h, --help         Show this help
EOF
}

run_inside_container() (
    local results=$1
    local work=/work/source
    local status=1
    local started
    started=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    mkdir -p "$results/studio" "$work"
    rm -rf "$results/Testing" "$results/studio"
    mkdir -p "$results/studio"
    rm -f "$results/studio-ctest.log" "$results/studio-junit.xml" \
        "$results/environment.txt" "$results/pimio-studio-linux-x64.tar.gz" \
        "$results/pimio-studio-linux-x64.tar.gz.tmp"
    exec > >(tee -a "$results/studio-ctest.log") 2>&1

    # shellcheck disable=SC2317
    write_environment() {
        {
            echo "pimio Studio (Tests B) Linux container run"
            echo "started : $started"
            echo "finished: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
            echo "commit  : ${PIMIO_SOURCE_COMMIT:-unknown}"
            echo "image   : ${PIMIO_IMAGE_NAME:-unknown}"
            echo "image id: ${PIMIO_IMAGE_ID:-unknown}"
            echo "display : ${QT_QPA_PLATFORM:-unknown} ${DISPLAY:-${WAYLAND_DISPLAY:-}}"
            echo "status  : $status"
        } > "$results/environment.txt"
    }

    # shellcheck disable=SC2317
    finish() {
        write_environment
        if tar --exclude=pimio-studio-linux-x64.tar.gz.tmp -C "$results" \
            -czf "$results/pimio-studio-linux-x64.tar.gz.tmp" . &&
            mv "$results/pimio-studio-linux-x64.tar.gz.tmp" \
                "$results/pimio-studio-linux-x64.tar.gz"; then
            return 0
        fi
        status=1
        rm -f "$results/pimio-studio-linux-x64.tar.gz.tmp"
        write_environment
        return 1
    }
    trap finish EXIT

    echo "== Clean working copy =="
    tar -C /source --exclude=.git --exclude=.cache --exclude=build -cf - . |
        tar -C "$work" -xf - || return
    cd "$work" || return

    echo "== Configure and build =="
    cmake --preset default -DPIMIO_REQUIRE_LORE=ON || return
    cmake --build --preset default || return

    echo "== Studio (Tests B) on $QT_QPA_PLATFORM =="
    export PIMIO_STUDIO_RESULTS="$results/studio"
    ctest --preset studio --output-on-failure \
        --output-junit "$results/studio-junit.xml"
    status=$?
    cp -a build/default/Testing "$results/" 2>/dev/null || true
    trap - EXIT
    if ! finish; then
        echo 'Could not create the Studio results archive.' >&2
        status=1
    fi
    return "$status"
)

if [[ ${1:-} == --inside-container ]]; then
    run_inside_container "${2:?missing container results directory}"
    exit $?
fi

repository_root=$(pimio_repository_root)
pimio_assert_pins_match_repository "$repository_root" || exit 1

display_mode=auto
output=
requested_engine=
image=$PIMIO_LOCAL_IMAGE
image_mode=build
while (($#)); do
    case "$1" in
        --display) display_mode=${2:?--display needs auto, x11, or wayland}; shift 2 ;;
        --output) output=${2:?--output needs a directory}; shift 2 ;;
        --engine) requested_engine=${2:?--engine needs docker or podman}; shift 2 ;;
        --image) image=${2:?--image needs a name}; shift 2 ;;
        --pull) image_mode=pull; shift ;;
        --use-image) image_mode=use; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$display_mode" == auto ]]; then
    if [[ -n ${WAYLAND_DISPLAY:-} && -n ${XDG_RUNTIME_DIR:-} &&
        -S ${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY} ]]; then
        display_mode=wayland
    elif [[ -n ${DISPLAY:-} ]]; then
        display_mode=x11
    else
        echo "No Wayland or X11 desktop was detected." >&2
        exit 1
    fi
fi

engine=$(pimio_select_container_engine "$requested_engine") || exit 1
if [[ -z "$output" ]]; then
    output="$repository_root/build/local-build/linux/studio-$(date -u +%Y%m%d-%H%M%S)"
elif [[ "$output" != /* ]]; then
    output="$PWD/$output"
fi
mkdir -p "$output"
output=$(CDPATH='' cd -- "$output" && pwd -P)

echo "== Container image ($engine) =="
pimio_prepare_image "$engine" "$image" "$image_mode" "$repository_root" || exit 1
image_id=$("$engine" image inspect --format '{{.Id}}' "$image")
source_commit=$(git -C "$repository_root" rev-parse HEAD 2>/dev/null || echo unknown)

container_args=(
    run --rm --platform linux/amd64
    --user "$(id -u):$(id -g)"
    --hostname "$(hostname)"
    --tmpfs "/work:rw,exec,mode=1777"
    -e HOME=/tmp
    -e "PIMIO_SOURCE_COMMIT=$source_commit"
    -e "PIMIO_IMAGE_NAME=$image"
    -e "PIMIO_IMAGE_ID=$image_id"
    --mount "type=bind,src=$repository_root,dst=/source,readonly"
    --mount "type=bind,src=$output,dst=/results"
)
if [[ "$engine" == podman &&
    $("$engine" info --format '{{.Host.Security.Rootless}}') == true ]]; then
    container_args+=(--userns=keep-id)
fi

case "$display_mode" in
    x11)
        [[ -n ${DISPLAY:-} ]] || { echo 'DISPLAY is not set.' >&2; exit 1; }
        [[ -d /tmp/.X11-unix ]] || { echo '/tmp/.X11-unix is missing.' >&2; exit 1; }
        container_args+=(
            -e "DISPLAY=$DISPLAY"
            -e QT_QPA_PLATFORM=xcb
            --mount "type=bind,src=/tmp/.X11-unix,dst=/tmp/.X11-unix,readonly"
        )
        xauthority=${XAUTHORITY:-$HOME/.Xauthority}
        if [[ -f "$xauthority" ]]; then
            container_args+=(
                -e XAUTHORITY=/tmp/pimio.Xauthority
                --mount "type=bind,src=$xauthority,dst=/tmp/pimio.Xauthority,readonly"
            )
        fi
        ;;
    wayland)
        [[ -n ${WAYLAND_DISPLAY:-} && -n ${XDG_RUNTIME_DIR:-} ]] ||
            { echo 'WAYLAND_DISPLAY and XDG_RUNTIME_DIR must be set.' >&2; exit 1; }
        [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] ||
            { echo "Wayland socket $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY is missing." >&2; exit 1; }
        container_args+=(
            -e "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
            -e "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
            -e QT_QPA_PLATFORM=wayland
            --mount "type=bind,src=$XDG_RUNTIME_DIR,dst=$XDG_RUNTIME_DIR,readonly"
        )
        ;;
    *) echo "Unknown display mode: $display_mode" >&2; exit 2 ;;
esac

echo "== Studio in clean container ($display_mode) =="
"$engine" "${container_args[@]}" "$image" \
    /source/tools/local-build/linux/run-studio.sh --inside-container /results
status=$?
echo "Results: $output"
exit "$status"
