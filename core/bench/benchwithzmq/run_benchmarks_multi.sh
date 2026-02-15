#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MULTI_PATTERNS="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_ROUTER_ROUTER_POLL,MULTI_PUBSUB"
MULTI_TRANSPORTS="tcp,tls,ws,wss"
IFS=',' read -r -a MULTI_PATTERN_LIST <<< "${MULTI_PATTERNS}"

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithzmq/run_benchmarks_multi.sh [options]

Run only multi-socket benchmark patterns.
Default PATTERN is:
  MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_ROUTER_ROUTER_POLL,MULTI_PUBSUB
By default, multi-bench keeps warmup at 3s and measure window at 10s.
By default, multi-bench uses transports: tcp,tls,ws,wss (can be overridden with --transport).

If a pattern is explicitly passed, it is forwarded as-is to run_benchmarks.sh.

Options:
  --pattern NAME        Benchmark pattern (default: all MULTI_* patterns above).
  --help                Show this help.
  --result              Write result file under core/bench/benchwithzmq/results/YYYYMMDD/.
  --runs N              Iterations per configuration (default: 1).
  --multi-warmup-seconds N
                        Optional override for multi warmup seconds (default 3).
  --multi-measure-seconds N
                        Optional override for multi measure seconds (default 10).
  --transport LIST       Transport override (default: tcp,tls,ws,wss).
  --transports LIST      Alias for --transport.
USAGE
}

HAS_EXPLICIT_TRANSPORT=0
HAS_EXPLICIT_RESULTS_TAG=0
EXPLICIT_PATTERNS=()
SCRIPT_ARGS=()

while [[ $# -gt 0 ]]; do
  arg="$1"
  case "${arg}" in
    -h|--help)
      usage
      exit 0
      ;;
    --transport|--transports)
      HAS_EXPLICIT_TRANSPORT=1
      if [[ $# -lt 2 ]]; then
        echo "Error: ${arg} requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --pattern)
      if [[ $# -lt 2 ]]; then
        echo "Error: --pattern requires a value." >&2
        exit 1
      fi
      IFS=',' read -r -a pattern_list <<< "$2"
      for p in "${pattern_list[@]}"; do
        if [[ -n "${p}" ]]; then
          EXPLICIT_PATTERNS+=( "${p}" )
        fi
      done
      shift 2
      ;;
    --results-tag|--baseline-tag)
      HAS_EXPLICIT_RESULTS_TAG=1
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --multi-warmup-seconds|--multi-measure-seconds)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --*)
      if [[ $# -ge 2 && "${2}" != --* ]]; then
        SCRIPT_ARGS+=( "$1" "$2" )
        shift 2
      else
        SCRIPT_ARGS+=( "$1" )
        shift
      fi
      ;;
    *)
      echo "Error: unknown positional argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

PATTERNS=("${MULTI_PATTERN_LIST[@]}")
if [[ "${#EXPLICIT_PATTERNS[@]}" -gt 0 ]]; then
  PATTERNS=("${EXPLICIT_PATTERNS[@]}")
fi

RUN_BASE_ARGS=()
if [[ "${HAS_EXPLICIT_RESULTS_TAG}" -eq 0 ]]; then
  RUN_BASE_ARGS+=(--results-tag "multi")
fi
RUN_BASE_ARGS+=(
  --multi-warmup-seconds "${BENCH_MULTI_WARMUP_SECONDS:-3}"
  --multi-measure-seconds "${BENCH_MULTI_MEASURE_SECONDS:-10}"
)
if [[ "${HAS_EXPLICIT_TRANSPORT}" -eq 0 && -z "${BENCH_TRANSPORTS:-}" ]]; then
  RUN_BASE_ARGS+=(--transport "${MULTI_TRANSPORTS}")
fi

for pattern in "${PATTERNS[@]}"; do
  echo "=== Running multi benchmark: ${pattern} ==="
  if ! BENCH_COMPARISON_SCRIPT="${SCRIPT_DIR}/multi/run_comparison.py" \
    BENCH_FAIL_FAST=1 \
    "${SCRIPT_DIR}/run_benchmarks.sh" \
    "${RUN_BASE_ARGS[@]}" \
    "${SCRIPT_ARGS[@]}" \
    --pattern "${pattern}"; then
    echo "Multi benchmark failed for ${pattern}" >&2
    exit 1
  fi
done
