#!/usr/bin/env bash
# MARE's Unreal Editor in Docker (Linux, NVIDIA GPU). Nothing runs on the host
# but Docker; the plugins are built into Plugins/*/Binaries/Linux.
#
#   docker/linux/mare.sh build     build the image, then the project's plugins
#   docker/linux/mare.sh editor [ARGS]  open the editor on your X server (Wayland: XWayland);
#                                  ARGS go to UnrealEditor, e.g. /Game/Lunar_Terrain_Yutu
#   docker/linux/mare.sh smoke     headless check: load both levels, check the plugins, quit
#   docker/linux/mare.sh shell     a shell in the container
#
# docker/linux/progress.sh watches a running editor (shaders, GPU, log, smoke results).
#
# UE_IMAGE overrides the base image (default ghcr.io/epicgames/unreal-engine:dev-slim-5.7).
# SHADER_RESERVE is the percentage of logical cores left free while shaders
# compile (default 12, so 14 of 16 threads compile; Unreal's own default is 50).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export MARE_SUITE="$(cd "$HERE/../../.." && pwd)"
PROJECT=/work/MARE-Lunar-Simulator/Moon_Simulator.uproject
UE='$UE_ROOT/Engine'
SHADERS="-ini:Engine:[DevOptions.Shaders]:PercentageUnusedShaderCompilingThreads=${SHADER_RESERVE:-12}"
# Vulkan bindless heap for typed-buffer views: Unreal's 65,536 default overflows
# here (the levels' 2,000 wheel-track decals alone hold ~16,000, and re-creating
# them in one frame, as "Save As" or a new wheel-track pool does, needs as many
# again before the old ones are freed), and a full heap crashes the editor with
# "Attemping to update invalid descriptor handle". D3D12 on Windows has ~1M.
BINDLESS="-ini:Engine:[ConsoleVariables]:r.Vulkan.Bindless.MaxResourceUniformTexelBufferCount=1048576"
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
        shift $(( $# > 0 ))   # "mare.sh" alone means editor
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT -vulkan '$SHADERS' '$BINDLESS' $(printf '%q ' "$@")" ;;
    smoke)
        image
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT -vulkan '$SHADERS' '$BINDLESS' -RenderOffscreen -unattended -nosplash -nosound -stdout -FullStdOutLogOutput -ExecutePythonScript=/work/MARE-Lunar-Simulator/docker/linux/smoke.py" ;;
    shell)
        image
        run unreal bash ;;
    *)
        echo "usage: $0 [build|editor|smoke|shell]" >&2; exit 2 ;;
esac
