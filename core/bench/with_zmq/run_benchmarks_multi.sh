#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MULTI_PATTERNS="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_STREAM"
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
Usage: core/bench/with_zmq/run_benchmarks_multi.sh [options]

Run multi-socket benchmark patterns in sequence.
Default patterns:
  MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_STREAM
Default transports:
  tcp,tls,ws,wss
  (with_zmq direct comparison is tcp-only; non-tcp is skipped with warning)

Options:
  --pattern NAME[,NAME...]     Pattern list (MULTI_* only)
  --multi-clients N            Force clients for all patterns
  --multi-inflight N           Alias for --inflight
  --help                       Show help

Other options are forwarded to:
  core/bench/with_zmq/multi/run_benchmarks.sh
USAGE
}

HAS_EXPLICIT_TRANSPORT=0
HAS_EXPLICIT_CLIENTS=0
MULTI_CLIENTS="${BENCH_MULTI_CLIENTS:-}"
MULTI_INFLIGHT="${BENCH_MULTI_INFLIGHT:-}"
TRANSPORTS="${BENCH_TRANSPORTS:-${MULTI_TRANSPORTS}}"
EXPLICIT_PATTERNS=()
SCRIPT_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
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
    --transports|--transport)
      HAS_EXPLICIT_TRANSPORT=1
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      TRANSPORTS="$2"
      shift 2
      ;;
    --multi-clients|--clients)
      HAS_EXPLICIT_CLIENTS=1
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

if [[ -n "${MULTI_INFLIGHT}" ]]; then
  RUN_BASE_ARGS+=(--inflight "${MULTI_INFLIGHT}")
fi
if [[ "${HAS_EXPLICIT_CLIENTS}" -eq 1 && -n "${MULTI_CLIENTS}" ]]; then
  RUN_BASE_ARGS+=(--clients "${MULTI_CLIENTS}")
fi

SHOW_TOTAL_TIME=1
IFS=',' read -r -a TRANSPORT_LIST <<< "${TRANSPORTS}"
for pattern in "${PATTERNS[@]}"; do
  if [[ "${pattern}" != MULTI_* ]]; then
    echo "Error: run_benchmarks_multi.sh accepts only MULTI_* patterns." >&2
    exit 1
  fi

  pattern_clients="${MULTI_CLIENTS:-}"
  if [[ -z "${pattern_clients}" && "${HAS_EXPLICIT_CLIENTS}" -eq 0 ]]; then
    if [[ "${pattern^^}" == "MULTI_STREAM" ]]; then
      pattern_clients="${BENCH_MULTI_DEFAULT_STREAM_CLIENTS:-1000}"
    else
      pattern_clients="${BENCH_MULTI_DEFAULT_CLIENTS:-1000}"
    fi
  fi

  for transport in "${TRANSPORT_LIST[@]}"; do
    if [[ -z "${transport}" ]]; then
      continue
    fi
    echo "=== Running multi benchmark: ${pattern} (${transport}) ==="
    if ! BENCH_SUPPRESS_TOTAL_TIME=1 \
      BENCH_MULTI_CLIENTS="${pattern_clients:-${BENCH_MULTI_CLIENTS:-}}" \
      "${SCRIPT_DIR}/multi/run_benchmarks.sh" \
      "${RUN_BASE_ARGS[@]}" \
      "${SCRIPT_ARGS[@]}" \
      --transport "${transport}" \
      --pattern "${pattern}"; then
      echo "Multi benchmark failed for ${pattern} (${transport})" >&2
      exit 1
    fi
  done
done
