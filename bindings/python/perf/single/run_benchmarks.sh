#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHONPATH_DIR="$(cd "$ROOT_DIR/../../src" && pwd)"

exec python "$ROOT_DIR/run_benchmarks.py" --pythonpath "$PYTHONPATH_DIR" "$@"
