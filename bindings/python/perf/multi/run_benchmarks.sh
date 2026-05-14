#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
has_transports=0
for arg in "$@"; do
    case "$arg" in
        --transports|--transports=*)
            has_transports=1
            break
            ;;
    esac
done

default_args=()
if [[ $has_transports -eq 0 && -z "${PERF_TRANSPORTS:-}" ]]; then
    default_args+=(--transports "tcp,tls,ws,wss")
fi

exec python -u "$SCRIPT_DIR/run_benchmarks.py" \
    "${default_args[@]}" \
    "$@"
