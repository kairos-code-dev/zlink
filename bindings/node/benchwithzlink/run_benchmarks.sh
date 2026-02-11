#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
export BINDING="node"
exec "${ROOT_DIR}/bindings/bench/common/run_benchmarks.sh" "$@"
