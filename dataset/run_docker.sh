#!/usr/bin/env bash
# The dataset generator in Docker (Linux): ./run_docker.sh ARGS runs
# `python generate.py ARGS`, e.g.
#   dataset/run_docker.sh all --config config_v2.yaml
#   dataset/run_docker.sh status --config config_v2.yaml
# A first argument ending in .py runs that script instead (extract.py, export_hf.py).
#
# The suite folder (this repository's parent) is mounted at its own path, so
# config paths such as ../../Unreal-Moon-Terrain-Generation resolve, and the
# Unreal container (docker/linux) sees the same files at the same paths. Host
# network for the dashboard API and Unreal's Remote Execution; the host's
# Docker socket for `docker exec` into the GNC containers. Runs as your user.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUITE="$(cd "$HERE/../.." && pwd)"
STUDIO="${TERRAIN_STUDIO:-$SUITE/Unreal-Moon-Terrain-Generation}"
IMAGE=mare/dataset:dev
SOCKET=/var/run/docker.sock

if [[ -n "${REBUILD:-}" ]] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -q -t "$IMAGE" --build-context studio="$STUDIO" "$HERE" >&2
fi
script=generate.py
if [[ "${1:-}" == *.py ]]; then script="$1"; shift; fi
exec docker run --rm --network host --user "$(id -u):$(id -g)" --group-add "$(stat -c %g "$SOCKET")" \
    -v "$SOCKET:$SOCKET" -v "$SUITE:$SUITE" -w "$HERE" "$IMAGE" python "$script" "$@"
