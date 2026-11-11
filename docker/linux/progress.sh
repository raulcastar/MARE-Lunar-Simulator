#!/usr/bin/env bash
# Live progress of the Unreal Editor started by mare.sh (editor or smoke):
# shader compile workers, shader cache size, GPU, the latest notable log lines
# and the smoke check's results. Ctrl-C to stop; --once prints a single frame.
#
#   docker/linux/progress.sh [--once] [interval seconds, default 5]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG="$HERE/../../Saved/Logs/Moon_Simulator.log"
once=; [[ "${1:-}" == --once ]] && { once=1; shift; }
interval="${1:-5}"

frame() {
    local c
    c=$(docker ps -q --filter label=com.docker.compose.project=mare-unreal | head -1)
    printf '\033[1mMARE Unreal Editor\033[0m  %s\n\n' "$(date +%T)"
    if [[ -z "$c" ]]; then
        echo "container: not running"
    else
        docker ps --filter id="$c" --format 'container: {{.Names}}  {{.Status}}'
        docker exec "$c" bash -c '
            ps -eo pcpu=,etime=,comm= | awk "
                /ShaderCompileWo/ { n++; if (\$1 > 50) busy++ }
                /UnrealEditor\$/  { ed = \$1 \"% CPU, up \" \$2 }
                END { printf \"editor: %s\nshader workers: %d running, %d busy\n\", ed ? ed : \"not running\", n, busy }"
            printf "shader/asset cache: %s\n" "$(du -sh ~/.config/Epic/UnrealEngine 2>/dev/null | cut -f1)"'
    fi
    nvidia-smi --query-gpu=memory.used,memory.total,utilization.gpu --format=csv,noheader \
        | awk -F', ' '{ print "GPU: " $1 " / " $2 ", " $3 " busy" }'
    if [[ -f "$LOG" ]]; then
        printf '\nlog: %s lines, last written %s\n' "$(wc -l < "$LOG")" "$(date -r "$LOG" +%T)"
        echo "--- latest notable lines"
        grep -E 'Error|Warning: |Fatal|Compiling|compile time exceeded|LoadMap|MARE_SMOKE|Python|Shutdown|Exiting' "$LOG" \
            | tail -8 | cut -c1-160
        local smoke
        smoke=$(grep -h 'MARE_SMOKE' "$LOG" | sed 's/.*MARE_SMOKE/MARE_SMOKE/')
        if [[ -n "$smoke" ]]; then printf -- '--- smoke check\n%s\n' "$smoke"; fi
    else
        printf '\nlog: %s not written yet\n' "$LOG"
    fi
}

if [[ -n "$once" ]]; then frame; exit; fi
while true; do out=$(frame); clear; echo "$out"; sleep "$interval"; done
