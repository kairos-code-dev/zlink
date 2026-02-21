#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MULTI_PATTERNS="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_GATEWAY,MULTI_SPOT,MULTI_STREAM"
MULTI_TRANSPORTS="tcp,tls,ws,wss"
IFS=',' read -r -a MULTI_PATTERN_LIST <<< "${MULTI_PATTERNS}"

SECONDS=0
SHOW_TOTAL_TIME=0
format_elapsed() {
  local total_sec="${1:-0}"
  local hours=$(( total_sec / 3600 ))
  local minutes=$(( (total_sec % 3600) / 60 ))
  local seconds=$(( total_sec % 60 ))
  if (( hours > 0 )); then
    printf "%dh %dm %ds" "${hours}" "${minutes}" "${seconds}"
  elif (( minutes > 0 )); then
    printf "%dm %ds" "${minutes}" "${seconds}"
  else
    printf "%ds" "${seconds}"
  fi
}
print_total_time() {
  if [[ "${SHOW_TOTAL_TIME}" -ne 1 ]]; then
    return
  fi
  if [[ "${BENCH_SUPPRESS_TOTAL_TIME:-0}" == "1" ]]; then
    return
  fi
  local status="${1:-0}"
  local elapsed="${SECONDS}"
  echo "Total benchmark time: $(format_elapsed "${elapsed}") (${elapsed}s, exit=${status})"
}
trap 'print_total_time $?' EXIT

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithzlink/run_benchmarks_multi.sh [options]

Run only multi-socket benchmark patterns.
Default PATTERN is:
  MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_GATEWAY,MULTI_SPOT,MULTI_STREAM
By default, multi-bench keeps warmup at 3s and measure window at 10s.
By default, multi-bench uses transports: tcp,tls,ws,wss (can be overridden with --transports).

If a pattern is explicitly passed, it is forwarded as-is to run_benchmarks.sh.

Options:
  --pattern NAME        Benchmark pattern (default: all MULTI_* patterns above).
  --help                Show this help.
  --result              Write result file under core/bench/benchwithzlink/results/YYYYMMDD/.
  --runs N              Iterations per configuration (default: 1).
  --multi-warmup-seconds N
                        Optional override for multi warmup seconds (default 3).
  --multi-measure-seconds N
                        Optional override for multi measure seconds (default 10).
  --multi-clients N       Override number of client sockets per pattern.
  --multi-inflight N      Override max in-flight messages.
  --multi-connect-concurrency N
                        Override concurrent connect count.
  --multi-drain-ms N      Override drain timeout after benchmark (ms).
USAGE
}

HAS_EXPLICIT_TRANSPORT=0
HAS_EXPLICIT_RESULTS_TAG=0
MULTI_WARMUP_SECONDS="${BENCH_MULTI_WARMUP_SECONDS:-3}"
MULTI_MEASURE_SECONDS="${BENCH_MULTI_MEASURE_SECONDS:-10}"
MULTI_CLIENTS="${BENCH_MULTI_CLIENTS:-}"
MULTI_INFLIGHT="${BENCH_MULTI_INFLIGHT:-}"
MULTI_CONNECT_CONCURRENCY="${BENCH_MULTI_CONNECT_CONCURRENCY:-}"
MULTI_DRAIN_MS="${BENCH_MULTI_DRAIN_MS:-}"
EXPLICIT_PATTERNS=()
SCRIPT_ARGS=()

while [[ $# -gt 0 ]]; do
  arg="$1"
  case "${arg}" in
    -h|--help)
      usage
      exit 0
      ;;
    --transports|--transport)
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
    --multi-warmup-seconds)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_WARMUP_SECONDS="${2}"
      shift 2
      ;;
    --multi-measure-seconds)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_MEASURE_SECONDS="${2}"
      shift 2
      ;;
    --multi-clients)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_CLIENTS="${2}"
      shift 2
      ;;
    --multi-inflight)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_INFLIGHT="${2}"
      shift 2
      ;;
    --multi-connect-concurrency)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_CONNECT_CONCURRENCY="${2}"
      shift 2
      ;;
    --multi-drain-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_DRAIN_MS="${2}"
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
if [[ "${HAS_EXPLICIT_TRANSPORT}" -eq 0 && -z "${BENCH_TRANSPORTS:-}" ]]; then
  RUN_BASE_ARGS+=(--transports "${MULTI_TRANSPORTS}")
fi
RUN_ENV=()
RUN_ENV+=(BENCH_MULTI_WARMUP_SECONDS="${MULTI_WARMUP_SECONDS}")
RUN_ENV+=(BENCH_MULTI_MEASURE_SECONDS="${MULTI_MEASURE_SECONDS}")
if [[ -n "${MULTI_CLIENTS}" ]]; then
  RUN_ENV+=(BENCH_MULTI_CLIENTS="${MULTI_CLIENTS}")
fi
if [[ -n "${MULTI_INFLIGHT}" ]]; then
  RUN_ENV+=(BENCH_MULTI_INFLIGHT="${MULTI_INFLIGHT}")
fi
if [[ -n "${MULTI_CONNECT_CONCURRENCY}" ]]; then
  RUN_ENV+=(BENCH_MULTI_CONNECT_CONCURRENCY="${MULTI_CONNECT_CONCURRENCY}")
fi
if [[ -n "${MULTI_DRAIN_MS}" ]]; then
  RUN_ENV+=(BENCH_MULTI_DRAIN_MS="${MULTI_DRAIN_MS}")
fi

SHOW_TOTAL_TIME=1
for pattern in "${PATTERNS[@]}"; do
  if [[ "${pattern^^}" == "MULTI_ROUTER_ROUTER_POLL" ]]; then
    echo "Error: MULTI_ROUTER_ROUTER_POLL is removed from multi benchmarks." >&2
    exit 1
  fi
  if [[ "${pattern}" != MULTI_* ]]; then
    echo "Error: run_benchmarks_multi.sh accepts only MULTI_* patterns." >&2
    exit 1
  fi
  echo "=== Running multi benchmark: ${pattern} ==="
  if ! BENCH_ALLOW_MULTI=1 \
    BENCH_COMPARISON_SCRIPT="${SCRIPT_DIR}/multi/run_comparison.py" \
    BENCH_FAIL_FAST=1 \
    BENCH_SUPPRESS_TOTAL_TIME=1 \
    env "${RUN_ENV[@]}" \
    "${SCRIPT_DIR}/run_benchmarks.sh" \
    "${RUN_BASE_ARGS[@]}" \
    "${SCRIPT_ARGS[@]}" \
    --pattern "${pattern}"; then
    echo "Multi benchmark failed for ${pattern}" >&2
    exit 1
  fi
done
