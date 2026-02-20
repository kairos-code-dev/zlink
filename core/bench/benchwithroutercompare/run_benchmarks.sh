#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithroutercompare/run_benchmarks.sh [options]

Run router compare benchmark: zlink vs libzmq vs gRPC.
Executes 1:1 echo then N:1 echo and prints combined results.

Options:
  --runs N              Iterations per configuration (default: 5).
  --clients N           N:1 test client count (default: 100).
  --build-dir PATH      Override build directory.
  --zlink-only          Run only zlink and compare with cached baselines.
  --refresh-cache       Refresh baseline caches.
  --run-cooldown-ms N   Sleep between runs in ms (default: 3000).
  --help                Show this help.
USAGE
}

RUNS=5
CLIENTS=100
BUILD_DIR=""
ZLINK_ONLY=0
REFRESH_CACHE=0
RUN_COOLDOWN_MS=3000

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --runs)
      if [[ $# -lt 2 ]]; then
        echo "Error: --runs requires a value." >&2
        exit 1
      fi
      RUNS="$2"
      shift 2
      ;;
    --clients)
      if [[ $# -lt 2 ]]; then
        echo "Error: --clients requires a value." >&2
        exit 1
      fi
      CLIENTS="$2"
      shift 2
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "Error: --build-dir requires a value." >&2
        exit 1
      fi
      BUILD_DIR="$2"
      shift 2
      ;;
    --zlink-only)
      ZLINK_ONLY=1
      shift
      ;;
    --refresh-cache)
      REFRESH_CACHE=1
      shift
      ;;
    --run-cooldown-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: --run-cooldown-ms requires a value." >&2
        exit 1
      fi
      RUN_COOLDOWN_MS="$2"
      shift 2
      ;;
    *)
      echo "Error: unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

PY_ARGS=()
PY_ARGS+=(--runs "${RUNS}")
PY_ARGS+=(--clients "${CLIENTS}")
PY_ARGS+=(--run-cooldown-ms "${RUN_COOLDOWN_MS}")

if [[ -n "${BUILD_DIR}" ]]; then
  PY_ARGS+=(--build-dir "${BUILD_DIR}")
fi
if [[ "${ZLINK_ONLY}" -eq 1 ]]; then
  PY_ARGS+=(--zlink-only)
fi
if [[ "${REFRESH_CACHE}" -eq 1 ]]; then
  PY_ARGS+=(--refresh-cache)
fi

echo "=== Router Compare Benchmark ==="
echo "  runs=${RUNS}, clients=${CLIENTS}"

exec python3 "${SCRIPT_DIR}/run_comparison.py" "${PY_ARGS[@]}"
