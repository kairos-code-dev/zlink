#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RUNNER="${ROOT_DIR}/bindings/perf/run_policy_bench.py"

ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration)
            if [[ $# -lt 2 ]]; then
                echo "missing value for --duration" >&2
                exit 1
            fi
            export PERF_SINGLE_DURATION_SECONDS="$2"
            shift 2
            ;;
        --clean-build)
            rm -rf "${SCRIPT_DIR}/single/build" "${SCRIPT_DIR}/multi/build"
            shift
            ;;
        *)
            ARGS+=("$1")
            shift
            ;;
    esac
done

exec python3 "${RUNNER}" --binding cpp --suite single "${ARGS[@]}"
