#!/usr/bin/env bash
# MARE's Unreal Editor in Docker (Linux, NVIDIA GPU). Nothing runs on the host
# but Docker; the plugins are built into Plugins/*/Binaries/Linux.
#
#   docker/linux/mare.sh build     build the image, then the project's plugins
#   docker/linux/mare.sh editor [ARGS]  open the editor on your X server (Wayland: XWayland);
#                                  ARGS go to UnrealEditor, e.g. /Game/Lunar_Terrain_Yutu
#   docker/linux/mare.sh headless [ARGS]  the editor without a window, starting Play by itself
#                                  (it renders as usual: every sensor works); Ctrl-C stops it
#   docker/linux/mare.sh play|stop|status  start or stop Play, or show the level and whether
#                                  Play runs, in a running editor (windowed or headless)
#   docker/linux/mare.sh smoke     headless check: load both levels, check the plugins, quit
#   docker/linux/mare.sh shell     a shell in the container
#
# docker/linux/progress.sh watches a running editor (shaders, GPU, log, smoke results).
#
# UE_IMAGE overrides the base image (default ghcr.io/epicgames/unreal-engine:dev-slim-5.7).
# A level given in ARGS (e.g. /Game/Lunar_Terrain_Yutu) must follow the project
# path directly: Unreal takes the map only from the first argument after it.
#
# SHADER_RESERVE is the percentage of logical cores left free while shaders
# compile (default 12, so 14 of 16 threads compile; Unreal's own default is 50).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export MARE_SUITE="$(cd "$HERE/../../.." && pwd)"
PROJECT=/work/MARE-Lunar-Simulator/Moon_Simulator.uproject
SCRIPTS=/work/MARE-Lunar-Simulator/docker/linux
UE='$UE_ROOT/Engine'
SHADERS="-ini:Engine:[DevOptions.Shaders]:PercentageUnusedShaderCompilingThreads=${SHADER_RESERVE:-12}"
# Vulkan bindless heap for typed-buffer views: Unreal's 65,536 default overflows
# here (the levels' 2,000 wheel-track decals alone hold ~16,000, and re-creating
# them in one frame, as "Save As" or a new wheel-track pool does, needs as many
# again before the old ones are freed), and a full heap crashes the editor with
# "Attemping to update invalid descriptor handle". D3D12 on Windows has ~1M.
BINDLESS="-ini:Engine:[ConsoleVariables]:r.Vulkan.Bindless.MaxResourceUniformTexelBufferCount=1048576"
# The editor's embedded browser (CEF) cannot start its GPU process in the
# container (ANGLE fails on Vulkan and GL alike): it retries about 20 times in
# the first minute, printing ANGLE errors, then stops. Ask it for software.
CEF="-ini:Engine:[ConsoleVariables]:r.CEFGPUAcceleration=0"
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
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT $(printf '%q ' "$@") -vulkan '$SHADERS' '$BINDLESS' '$CEF'" ;;
    headless)
        image
        shift
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT $(printf '%q ' "$@") -vulkan '$SHADERS' '$BINDLESS' '$CEF' -RenderOffscreen -unattended -nosplash -nosound '-ExecCmds=py $SCRIPTS/autoplay.py'" ;;
    play|stop|status)
        image
        run -T -e MARE_SUITE unreal bash -lc "$UE/Binaries/ThirdParty/Python3/Linux/bin/python3 -I $SCRIPTS/remote.py $1" ;;
    smoke)
        image
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT -vulkan '$SHADERS' '$BINDLESS' '$CEF' -RenderOffscreen -unattended -nosplash -nosound -stdout -FullStdOutLogOutput -ExecutePythonScript=$SCRIPTS/smoke.py" ;;
    shell)
        image
        run unreal bash ;;
    *)
        echo "usage: $0 [build|editor|headless|play|stop|status|smoke|shell]" >&2; exit 2 ;;
esac
