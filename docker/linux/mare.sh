#!/usr/bin/env bash
# MARE's Unreal Editor in Docker (Linux, NVIDIA GPU). Nothing runs on the host
# but Docker; the plugins are built into Plugins/*/Binaries/Linux.
#
#   docker/linux/mare.sh build     build the image, then the project's plugins (and, the
#                                  first time, URLab's native libraries: see below)
#   docker/linux/mare.sh editor [ARGS]  open the editor on your X server (Wayland: XWayland);
#                                  ARGS go to UnrealEditor, e.g. /Game/Lunar_Terrain_Yutu
#   docker/linux/mare.sh headless [ARGS]  the editor without a window, starting Play by itself
#                                  (it renders as usual: every sensor works); Ctrl-C stops it
#   docker/linux/mare.sh play|stop|status  start or stop Play, or show the level and whether
#                                  Play runs, in a running editor (windowed or headless)
#   docker/linux/mare.sh smoke     headless check: load every level, check the plugins, quit
#   docker/linux/mare.sh shell     a shell in the container
#
# docker/linux/progress.sh watches a running editor (shaders, GPU, log, smoke results).
#
# UE_IMAGE overrides the base image (default ghcr.io/epicgames/unreal-engine:dev-slim-5.7.4).
# A level given in ARGS (e.g. /Game/Lunar_Terrain_Yutu) must follow the project
# path directly: Unreal takes the map only from the first argument after it.
#
# QUALITY=high (the default) renders the rover's cameras at the best quality,
# at the cost of simulation speed; QUALITY=project keeps the project's own
# rendering settings (as on Windows). See the list below.
#
# SHADER_RESERVE is the percentage of logical cores left free while shaders
# compile (default 12, so 14 of 16 threads compile; Unreal's own default is 50).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export MARE_SUITE="$(cd "$HERE/../../.." && pwd)"
PROJECT=/work/MARE-Lunar-Simulator/Moon_Simulator.uproject
SCRIPTS=/work/MARE-Lunar-Simulator/docker/linux
UE='$UE_ROOT/Engine'
# MuJoCo physics (Lunar_Terrain_Raph_MuJoCo) comes from the Unreal Robotics Lab
# plugin, a git submodule. Its native libraries (MuJoCo, CoACD, libzmq) are built
# once with the engine's clang and libc++, as the system gcc's would not link into
# the plugin, on every core (URLab's scripts otherwise use one), then linked next
# to the plugin's .so, where the editor loads them.
URLAB=/work/MARE-Lunar-Simulator/Plugins/UnrealRoboticsLab
URLAB_DEPS="git -C /work/MARE-Lunar-Simulator submodule update --init Plugins/UnrealRoboticsLab \
 && { ls $URLAB/third_party/install/MuJoCo/lib/libmujoco.so* >/dev/null 2>&1 \
      || CMAKE_BUILD_PARALLEL_LEVEL=\$(nproc) \
         bash $URLAB/third_party/build_all.sh --engine \$UE_ROOT; }"
URLAB_RUNTIME="bash $URLAB/Scripts/setup_runtime_linux.sh"
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
# Camera quality. The cameras are scene captures rendered a few times a
# simulated second at 640x480, and several of the project's settings, made for
# the editor viewport, degrade them:
HIGH_QUALITY=(
    r.LocalExposure=0                 # local exposure (Fusion, from the level's post-process volume)
                                      # posterises captures this small into flat patches
    r.AntiAliasingMethod=1            # FXAA: temporal AA smears renders 0.2 s apart (the dataset's choice too)
    r.MotionBlurQuality=0             # no motion blur: between renders this far apart it streaks the ground
    r.MotionBlur.Max=0
    r.Shadow.Virtual.ResolutionLodBiasDirectional=-2.5   # finer sun shadows (the dataset's values)
    r.Shadow.Virtual.MaxPhysicalPages=8192               # ...with room for them, so distant ones stay
    r.Shadow.Virtual.SMRT.RayCountDirectional=0          # sharp sun shadows, without soft-shadow dither dots
    r.MaxAnisotropy=16                # sharp ground texture at grazing angles
    r.Streaming.PoolSize=3000         # full-resolution textures for the cameras too (the texture
    r.Streaming.FullyLoadUsedTextures=1   # streamer sizes mips for the editor viewport, not captures)
    r.Nanite.MaxPixelsPerEdge=0.5     # finer distant geometry
)
# Console commands at startup (-ExecCmds, comma-separated). r.ExposureOffset is a
# "cheat" variable, which an ini override may not set: without local exposure
# the image is brighter, and this keeps its brightness.
EXEC=""
OPTS="-vulkan '$SHADERS' '$BINDLESS' '$CEF'"
case "${QUALITY:-high}" in
    high) for c in "${HIGH_QUALITY[@]}"; do OPTS+=" '-ini:Engine:[ConsoleVariables]:$c'"; done
          EXEC="r.ExposureOffset -1.5" ;;
    project) ;;
    *) echo "QUALITY must be high or project" >&2; exit 2 ;;
esac
compose=(docker compose -f "$HERE/compose.yaml")

image() {
    if [[ -n "${REBUILD:-}" ]] || ! docker image inspect mare/unreal:5.7 >/dev/null 2>&1; then
        "${compose[@]}" build
    fi
}
run() { "${compose[@]}" run --rm "$@"; }

# The editor's cache server (zenserver) records itself in /dev/shm/UnrealEngineZen,
# which the containers share with the host (ipc: host, for the X display). An
# editor that was killed leaves its entry there, naming a process id that the
# next container gives to its own new zenserver: the next editor then shuts
# that down as a stranger, again and again, and dies ("DerivedDataBackends.cpp
# Line 215"). With no editor container running, every entry is stale.
clear_stale_state() {
    if ! docker ps -q --filter label=com.docker.compose.project=mare-unreal | grep -q .; then
        rm -f /dev/shm/UnrealEngineZen /dev/shm/UnrealTraceServer
    fi
}

case "${1:-editor}" in
    build)
        # No target name: UnrealBuildTool picks the project's editor target, which
        # URLab requires (it enables engine plugins that are off by default), and
        # UnrealEditor without it. Naming UnrealEditor would build both and fail.
        "${compose[@]}" build
        run unreal bash -lc "$URLAB_DEPS && $UE/Build/BatchFiles/Linux/Build.sh Linux Development -Project=$PROJECT -TargetType=Editor -Progress && $URLAB_RUNTIME" ;;
    editor)
        image
        shift $(( $# > 0 ))   # "mare.sh" alone means editor
        clear_stale_state
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT $(printf '%q ' "$@") $OPTS ${EXEC:+'-ExecCmds=$EXEC'}" ;;
    headless)
        image
        shift
        clear_stale_state
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT $(printf '%q ' "$@") $OPTS -RenderOffscreen -unattended -nosplash -nosound '-ExecCmds=${EXEC:+$EXEC,}py $SCRIPTS/autoplay.py'" ;;
    play|stop|status)
        image
        run -T -e MARE_SUITE unreal bash -lc "$UE/Binaries/ThirdParty/Python3/Linux/bin/python3 -I $SCRIPTS/remote.py $1" ;;
    smoke)
        image
        clear_stale_state
        run unreal bash -lc "$UE/Binaries/Linux/UnrealEditor $PROJECT $OPTS -RenderOffscreen -unattended -nosplash -nosound -stdout -FullStdOutLogOutput -ExecutePythonScript=$SCRIPTS/smoke.py" ;;
    shell)
        image
        run unreal bash ;;
    *)
        echo "usage: $0 [build|editor|headless|play|stop|status|smoke|shell]" >&2; exit 2 ;;
esac
