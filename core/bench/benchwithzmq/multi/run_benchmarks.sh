#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)/results"

DEFAULT_PATTERN="router_router"
DEFAULT_TRANSPORT="tcp"

normalize_pattern() {
  local raw="${1:-}"
  if [[ -z "${raw}" ]]; then
    return 1
  fi

  local up
  up="$(echo "${raw}" | tr '[:lower:]' '[:upper:]')"
  up="${up#MULTI_}"
  up="${up#MULT_}"

  case "${up}" in
    DEALER_DEALER) echo "MULTI_DEALER_DEALER" ;;
    DEALER_ROUTER) echo "MULTI_DEALER_ROUTER" ;;
    ROUTER_ROUTER) echo "MULTI_ROUTER_ROUTER" ;;
    ROUTER_ROUTER_POLL) echo "MULTI_ROUTER_ROUTER_POLL" ;;
    PUBSUB) echo "MULTI_PUBSUB" ;;
    *) return 1 ;;
  esac
}

display_pattern() {
  local internal="${1:-}"
  internal="${internal#MULTI_}"
  echo "$(echo "${internal}" | tr '[:upper:]' '[:lower:]')"
}

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithzmq/multi/run_benchmarks.sh [options]

Run one multi benchmark pattern with one transport and compare libzmq vs zlink.

Options:
  --pattern NAME                One pattern (default: router_router)
                                Allowed: dealer_dealer, dealer_router,
                                         router_router, router_router_poll, pubsub
                                Also accepts MULTI_* legacy names.
  --runs N                      Iterations per configuration (default: 1)
  --transport NAME              One transport (default: tcp)
  --transports NAME             Alias for --transport
  --zlink-only                  Re-measure only zlink (compare with cached libzmq)
  --single [N]                  Also run matching single pattern after multi
                                N omitted: use --runs value
                                N provided: use N for single runs
  --result                      Write output to core/bench/benchwithzmq/results/YYYYMMDD/
  --refresh-std-cache           Refresh libzmq cache during this run
  --std-cache-file PATH         libzmq cache file path
  --duration N                  Override BENCH_MULTI_DURATION_SECONDS (default: 5)
  --settle-ms N                 Override BENCH_MULTI_SETTLE_MS (default: 500)
  --clients N                   Override BENCH_MULTI_CLIENTS
  --inflight N                  Per-client inflight window (global=clients*N, default: 30)
  --inflight-per-client N       Alias for --inflight
  --hwm N                       Override BENCH_MULTI_HWM (default: 300000)
  --monitor-hwm N               Override BENCH_MULTI_MONITOR_HWM (monitor queue HWM)
  --connect-ready-timeout-ms N Override BENCH_MULTI_CONNECT_READY_TIMEOUT_MS (default: 5000)
  --drain-ms N                  Override BENCH_MULTI_DRAIN_MS
  --recv-batch N                Override BENCH_MULTI_RECV_BATCH
  --send-workers N              Override BENCH_MULTI_SEND_WORKERS (default: 2)
  --send-backoff-us N           Override BENCH_MULTI_SEND_BACKOFF_US
  --io-threads N                Override BENCH_IO_THREADS (auto: 4 when clients>=512)
  --msg-sizes LIST              Override BENCH_MSG_SIZES (e.g. 1024,65536)
  --build-dir PATH              Forwarded to run_comparison.py
  --pin-cpu                     Forwarded to run_comparison.py
  --help                        Show help
USAGE
}

RUNS=1
TRANSPORT="${BENCH_TRANSPORTS:-${DEFAULT_TRANSPORT}}"
MSG_SIZES="${BENCH_MSG_SIZES:-}"
BUILD_DIR=""
PIN_CPU=0
ZLINK_ONLY=0
RUN_SINGLE=0
SINGLE_RUNS=""
RESULT_TO_FILE=0
REFRESH_STD_CACHE=0
STD_CACHE_FILE=""
PATTERN_RAW="${DEFAULT_PATTERN}"

MULTI_DURATION_SECONDS="${BENCH_MULTI_DURATION_SECONDS:-${BENCH_MULTI_MEASURE_SECONDS:-5}}"
MULTI_SETTLE_MS="${BENCH_MULTI_SETTLE_MS:-500}"
MULTI_CLIENTS="${BENCH_MULTI_CLIENTS:-}"
MULTI_INFLIGHT="${BENCH_MULTI_INFLIGHT:-}"
MULTI_HWM="${BENCH_MULTI_HWM:-300000}"
MULTI_MONITOR_HWM="${BENCH_MULTI_MONITOR_HWM:-}"
MULTI_CONNECT_READY_TIMEOUT_MS="${BENCH_MULTI_CONNECT_READY_TIMEOUT_MS:-5000}"
MULTI_DRAIN_MS="${BENCH_MULTI_DRAIN_MS:-300}"
MULTI_RECV_BATCH="${BENCH_MULTI_RECV_BATCH:-64}"
MULTI_SEND_WORKERS="${BENCH_MULTI_SEND_WORKERS:-2}"
MULTI_SEND_BACKOFF_US="${BENCH_MULTI_SEND_BACKOFF_US:-}"
MULTI_IO_THREADS="${BENCH_IO_THREADS:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --pattern)
      PATTERN_RAW="${2:-}"
      shift 2
      ;;
    --runs)
      RUNS="${2:-}"
      shift 2
      ;;
    --transport|--transports)
      TRANSPORT="${2:-}"
      shift 2
      ;;
    --msg-sizes)
      MSG_SIZES="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --zlink-only)
      ZLINK_ONLY=1
      shift
      ;;
    --single)
      RUN_SINGLE=1
      if [[ $# -ge 2 && "${2:-}" =~ ^[0-9]+$ ]]; then
        SINGLE_RUNS="${2:-}"
        shift 2
      else
        shift
      fi
      ;;
    --result)
      RESULT_TO_FILE=1
      shift
      ;;
    --refresh-std-cache)
      REFRESH_STD_CACHE=1
      shift
      ;;
    --std-cache-file)
      STD_CACHE_FILE="${2:-}"
      shift 2
      ;;
    --pin-cpu)
      PIN_CPU=1
      shift
      ;;
    --duration)
      MULTI_DURATION_SECONDS="${2:-}"
      shift 2
      ;;
    --settle-ms|--multi-settle-ms)
      MULTI_SETTLE_MS="${2:-}"
      shift 2
      ;;
    --clients|--multi-clients)
      MULTI_CLIENTS="${2:-}"
      shift 2
      ;;
    --inflight|--inflight-per-client|--multi-inflight)
      MULTI_INFLIGHT="${2:-}"
      shift 2
      ;;
    --hwm|--multi-hwm)
      MULTI_HWM="${2:-}"
      shift 2
      ;;
    --monitor-hwm|--multi-monitor-hwm)
      MULTI_MONITOR_HWM="${2:-}"
      shift 2
      ;;
    --connect-ready-timeout-ms|--multi-connect-ready-timeout-ms)
      MULTI_CONNECT_READY_TIMEOUT_MS="${2:-}"
      shift 2
      ;;
    --drain-ms|--multi-drain-ms)
      MULTI_DRAIN_MS="${2:-}"
      shift 2
      ;;
    --recv-batch|--multi-recv-batch)
      MULTI_RECV_BATCH="${2:-}"
      shift 2
      ;;
    --send-workers|--multi-send-workers)
      MULTI_SEND_WORKERS="${2:-}"
      shift 2
      ;;
    --send-backoff-us|--multi-send-backoff-us)
      MULTI_SEND_BACKOFF_US="${2:-}"
      shift 2
      ;;
    --io-threads)
      MULTI_IO_THREADS="${2:-}"
      shift 2
      ;;
    *)
      echo "Error: unknown option '$1'" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "${TRANSPORT}" == *","* ]]; then
  echo "Error: only one transport is supported. Use a single value (e.g. tcp)." >&2
  exit 1
fi
if [[ -z "${TRANSPORT}" ]]; then
  echo "Error: --transport requires a non-empty value." >&2
  exit 1
fi

if [[ "${PATTERN_RAW}" == *","* ]]; then
  echo "Error: only one pattern is supported. Use a single value (e.g. router_router)." >&2
  exit 1
fi
PATTERN_INTERNAL="$(normalize_pattern "${PATTERN_RAW}")" || {
  echo "Error: unsupported pattern '${PATTERN_RAW}'." >&2
  echo "Supported: dealer_dealer, dealer_router, router_router, router_router_poll, pubsub" >&2
  exit 1
}
PATTERN_TAG="$(display_pattern "${PATTERN_INTERNAL}")"

if [[ "${RESULT_TO_FILE}" -eq 1 ]]; then
  DATE_DIR="$(date +%Y%m%d)"
  TS="$(date +%Y%m%d_%H%M%S)"
  PLATFORM="linux"
  case "$(uname -s)" in
    Darwin*)
      PLATFORM="macos"
      ;;
    MINGW*|MSYS*|CYGWIN*)
      PLATFORM="windows"
      ;;
  esac

  RESULT_PATTERN_TAG="${PATTERN_TAG}"

  OUTPUT_DIR="${RESULTS_ROOT}/${DATE_DIR}"
  mkdir -p "${OUTPUT_DIR}"
  OUTPUT_FILE="${OUTPUT_DIR}/bench_${PLATFORM}_${RESULT_PATTERN_TAG}_${TS}.txt"
  exec > >(tee "${OUTPUT_FILE}") 2>&1
  echo "Saving benchmark output to: ${OUTPUT_FILE}"
fi

export BENCH_TRANSPORTS="${TRANSPORT}"
export BENCH_MULTI_DURATION_SECONDS="${MULTI_DURATION_SECONDS}"
export BENCH_MULTI_MEASURE_SECONDS="${MULTI_DURATION_SECONDS}"
export BENCH_MULTI_SETTLE_MS="${MULTI_SETTLE_MS}"
export BENCH_MULTI_DRAIN_MS="${MULTI_DRAIN_MS}"
export BENCH_MULTI_RECV_BATCH="${MULTI_RECV_BATCH}"
export BENCH_MULTI_HWM="${MULTI_HWM}"
export BENCH_MULTI_CONNECT_READY_TIMEOUT_MS="${MULTI_CONNECT_READY_TIMEOUT_MS}"
if [[ -n "${MULTI_MONITOR_HWM}" ]]; then
  export BENCH_MULTI_MONITOR_HWM="${MULTI_MONITOR_HWM}"
fi

effective_clients="${MULTI_CLIENTS:-${BENCH_MULTI_CLIENTS:-100}}"
effective_inflight="${MULTI_INFLIGHT:-${BENCH_MULTI_INFLIGHT:-30}}"
if [[ "${effective_clients}" =~ ^[0-9]+$ && "${effective_inflight}" =~ ^[0-9]+$ ]]; then
  echo "Config: clients=${effective_clients}, inflight_per_client=${effective_inflight}, global_inflight=$(( effective_clients * effective_inflight ))"
fi
echo "Config: duration=${MULTI_DURATION_SECONDS}, settle_ms=${MULTI_SETTLE_MS}, drain_ms=${MULTI_DRAIN_MS}"

if [[ -n "${MULTI_IO_THREADS}" ]]; then
  export BENCH_IO_THREADS="${MULTI_IO_THREADS}"
elif [[ -z "${BENCH_IO_THREADS:-}" && "${effective_clients}" =~ ^[0-9]+$ && "${effective_clients}" -ge 512 ]]; then
  export BENCH_IO_THREADS="4"
fi
if [[ -n "${BENCH_IO_THREADS:-}" ]]; then
  echo "Config: io_threads=${BENCH_IO_THREADS}"
fi

if [[ -n "${MULTI_CLIENTS}" ]]; then
  export BENCH_MULTI_CLIENTS="${MULTI_CLIENTS}"
fi
if [[ -n "${MULTI_INFLIGHT}" ]]; then
  export BENCH_MULTI_INFLIGHT="${MULTI_INFLIGHT}"
fi
if [[ -n "${MULTI_SEND_WORKERS}" ]]; then
  export BENCH_MULTI_SEND_WORKERS="${MULTI_SEND_WORKERS}"
fi
if [[ -n "${MULTI_SEND_BACKOFF_US}" ]]; then
  export BENCH_MULTI_SEND_BACKOFF_US="${MULTI_SEND_BACKOFF_US}"
fi
if [[ -n "${MSG_SIZES}" ]]; then
  export BENCH_MSG_SIZES="${MSG_SIZES}"
fi

EXTRA_ARGS=(--runs "${RUNS}")
if [[ -n "${BUILD_DIR}" ]]; then
  EXTRA_ARGS+=(--build-dir "${BUILD_DIR}")
fi
if [[ "${PIN_CPU}" -eq 1 ]]; then
  EXTRA_ARGS+=(--pin-cpu)
fi
if [[ "${ZLINK_ONLY}" -eq 1 ]]; then
  EXTRA_ARGS+=(--zlink-only)
fi
if [[ "${REFRESH_STD_CACHE}" -eq 1 ]]; then
  EXTRA_ARGS+=(--refresh-std-cache)
fi
if [[ -n "${STD_CACHE_FILE}" ]]; then
  EXTRA_ARGS+=(--std-cache-file "${STD_CACHE_FILE}")
fi

echo "=== Running multi benchmark: ${PATTERN_TAG} ==="
python3 "${SCRIPT_DIR}/run_comparison.py" "${PATTERN_INTERNAL}" "${EXTRA_ARGS[@]}"

if [[ "${RUN_SINGLE}" -eq 1 ]]; then
  case "${PATTERN_INTERNAL}" in
    MULTI_DEALER_DEALER)
      SINGLE_PATTERN="dealer_dealer"
      ;;
    MULTI_DEALER_ROUTER)
      SINGLE_PATTERN="dealer_router"
      ;;
    MULTI_ROUTER_ROUTER)
      SINGLE_PATTERN="router_router"
      ;;
    MULTI_ROUTER_ROUTER_POLL)
      SINGLE_PATTERN="router_router_poll"
      ;;
    MULTI_PUBSUB)
      SINGLE_PATTERN="pubsub"
      ;;
    *)
      SINGLE_PATTERN=""
      ;;
  esac

  if [[ -z "${SINGLE_PATTERN}" ]]; then
    echo "Warning: no matching single pattern for ${PATTERN_INTERNAL}; skipping --single." >&2
    exit 0
  fi

  SINGLE_RUNS_EFFECTIVE="${SINGLE_RUNS:-${RUNS}}"
  SINGLE_SCRIPT="${SCRIPT_DIR}/../single/run_benchmarks.sh"
  SINGLE_ARGS=(--pattern "${SINGLE_PATTERN}" --transport "${TRANSPORT}" --runs "${SINGLE_RUNS_EFFECTIVE}")

  if [[ -n "${MSG_SIZES}" ]]; then
    SINGLE_ARGS+=(--msg-sizes "${MSG_SIZES}")
  fi
  if [[ -n "${BUILD_DIR}" ]]; then
    SINGLE_ARGS+=(--build-dir "${BUILD_DIR}")
  fi
  if [[ "${PIN_CPU}" -eq 1 ]]; then
    SINGLE_ARGS+=(--pin-cpu)
  fi
  if [[ "${RESULT_TO_FILE}" -eq 1 ]]; then
    SINGLE_ARGS+=(--result)
  fi

  echo "=== Running matching single benchmark: ${SINGLE_PATTERN} (runs=${SINGLE_RUNS_EFFECTIVE}) ==="
  "${SINGLE_SCRIPT}" "${SINGLE_ARGS[@]}"
fi
