#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
exec env ZLINK_PERF_BINDING="java" "${ROOT_DIR}/bindings/perf/run_binding_multi.sh" "$@"
