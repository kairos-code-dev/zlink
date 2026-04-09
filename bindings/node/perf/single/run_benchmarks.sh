#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

REUSE_BUILD=0
CLEAN_BUILD=0
for arg in "$@"; do
  case "${arg}" in
    --reuse-build) REUSE_BUILD=1 ;;
    --clean-build) CLEAN_BUILD=1 ;;
  esac
done

if [ "${REUSE_BUILD}" -eq 1 ] && [ "${CLEAN_BUILD}" -eq 1 ]; then
  echo "Error: --reuse-build and --clean-build are mutually exclusive." >&2
  exit 1
fi

if [ "${CLEAN_BUILD}" -eq 1 ]; then
  rm -rf "$ROOT_DIR/dist-tools"
fi

if [ "${REUSE_BUILD}" -eq 0 ]; then
  npm run build
elif [ ! -f "$ROOT_DIR/dist-tools/perf/common/perf_metric_worker.js" ]; then
  echo "Error: --reuse-build requested but dist-tools output is missing." >&2
  exit 1
fi

node dist-tools/perf/single/run_benchmarks.js "$@"
