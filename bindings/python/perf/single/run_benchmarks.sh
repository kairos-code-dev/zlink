#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$ROOT_DIR/../../../.." && pwd)"
CORE_RUNTIME="$REPO_DIR/core/build/lib/libzlink.so"
show_help=0
for arg in "$@"; do
    case "$arg" in
        -h|--help)
            show_help=1
            ;;
    esac
done

if [[ $show_help -eq 0 ]]; then
    if [[ ! -f "$CORE_RUNTIME" ]]; then
        echo "Error: core runtime is missing: $CORE_RUNTIME" >&2
        echo "Build it first with: cmake --build core/build" >&2
        exit 1
    fi
    if find "$REPO_DIR/core/include" "$REPO_DIR/core/src" -type f -newer "$CORE_RUNTIME" | grep -q .; then
        echo "Error: core runtime is older than core/include or core/src." >&2
        echo "Rebuild it first with: cmake --build core/build" >&2
        echo "Runtime checked: $CORE_RUNTIME" >&2
        exit 1
    fi
    echo "Perf runtime libzlink: $(realpath "$CORE_RUNTIME")"
fi

export ZLINK_LIBRARY_PATH="${ZLINK_LIBRARY_PATH:-$CORE_RUNTIME}"

exec python -u "$ROOT_DIR/run_benchmarks.py" "$@"
