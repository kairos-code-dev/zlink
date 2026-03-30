#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHONPATH_DIR="$(cd "$SCRIPT_DIR/../../src" && pwd)"
exec python "$SCRIPT_DIR/run_benchmarks.py" --pythonpath "$PYTHONPATH_DIR" "$@"
