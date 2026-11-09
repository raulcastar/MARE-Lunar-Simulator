#!/usr/bin/env bash
# MARE's Unreal Editor in Docker (Linux, NVIDIA GPU). Nothing runs on the host
# but Docker; the plugins are built into Plugins/*/Binaries/Linux.
#
#   docker/linux/mare.sh build     build the image, then the project's plugins
#   docker/linux/mare.sh editor    open the editor on your X server (Wayland: XWayland)
#   docker/linux/mare.sh smoke     headless check: load both levels, check the plugins, quit
#   docker/linux/mare.sh shell     a shell in the container
#
# UE_IMAGE overrides the base image (default ghcr.io/epicgames/unreal-engine:dev-slim-5.7).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export MARE_SUITE="$(cd "$HERE/../../.." && pwd)"
PROJECT=/work/MARE-Lunar-Simulator/Moon_Simulator.uproject
UE='$UE_ROOT/Engine'
compose=(docker compose -f "$HERE/compose.yaml")

image() {
    if [[ -n "${REBUILD:-}" ]] || ! docker image inspect mare/unreal:5.7 >/dev/null 2>&1; then
        "${compose[@]}" build
    fi
}
run() { "${compose[@]}" run --rm "$@"; }

case "${1:-editor}" in
    build)
        "${compose[@]}" build
        run unreal bash -lc "$UE/Build/BatchFiles/Linux/Build.sh UnrealEditor Linux Development -Project=$PROJECT -TargetType=Editor -Progress" ;;
    editor)
        image
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT -vulkan" ;;
    smoke)
        image
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT -vulkan -RenderOffscreen -unattended -nosplash -nosound -stdout -FullStdOutLogOutput -ExecutePythonScript=/work/MARE-Lunar-Simulator/docker/linux/smoke.py" ;;
    shell)
        image
        run unreal bash ;;
    *)
        echo "usage: $0 [build|editor|smoke|shell]" >&2; exit 2 ;;
esac
