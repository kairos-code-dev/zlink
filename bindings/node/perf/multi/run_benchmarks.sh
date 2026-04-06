#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if [ ! -f "$ROOT_DIR/dist-tools/perf/common/perf_metric_worker.js" ]; then
  npm run build
fi

node dist-tools/perf/multi/run_benchmarks.js "$@"
