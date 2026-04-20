#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHONPATH_DIR="$(cd "$SCRIPT_DIR/../../src" && pwd)"

has_transports=0
for arg in "$@"; do
    if [[ "$arg" == "--transports" || "$arg" == --transports=* ]]; then
        has_transports=1
        break
    fi
done

if [[ -z "${PERF_TRANSPORTS:-}" && $has_transports -eq 0 ]]; then
    exec python -u "$SCRIPT_DIR/run_benchmarks.py" \
        --pythonpath "$PYTHONPATH_DIR" \
        --transports tcp,tls,ws,wss \
        "$@"
fi

exec python -u "$SCRIPT_DIR/run_benchmarks.py" --pythonpath "$PYTHONPATH_DIR" "$@"
