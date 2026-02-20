#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)/results"

DEFAULT_PATTERN="stream"
DEFAULT_TRANSPORT="tcp"

normalize_pattern() {
  local raw="${1:-}"
  local up
  up="$(echo "${raw}" | tr '[:lower:]' '[:upper:]')"
  up="${up#MULTI_}"
  case "${up}" in
    DEALER_DEALER) echo "MULTI_DEALER_DEALER" ;;
    DEALER_ROUTER) echo "MULTI_DEALER_ROUTER" ;;
    ROUTER_ROUTER) echo "MULTI_ROUTER_ROUTER" ;;
    PUBSUB) echo "MULTI_PUBSUB" ;;
    STREAM) echo "MULTI_STREAM" ;;
    *) return 1 ;;
  esac
}

display_pattern() {
  local p="${1:-}"
  p="${p#MULTI_}"
  echo "$(echo "${p}" | tr '[:upper:]' '[:lower:]')"
}

is_uint() {
  local value="${1:-}"
  [[ "${value}" =~ ^[0-9]+$ ]]
}

ensure_nofile_limit() {
  local clients="${1:-}"
  if [[ "${BENCH_SKIP_NOFILE_CHECK:-0}" == "1" ]]; then
    return 0
  fi
  if ! is_uint "${clients}"; then
    return 0
  fi

  local required=$(( clients * 2 + 4096 ))
  local soft
  local hard
  soft="$(ulimit -Sn 2>/dev/null || true)"
  hard="$(ulimit -Hn 2>/dev/null || true)"
  if [[ -z "${soft}" || -z "${hard}" ]]; then
    return 0
  fi

  if [[ "${soft}" == "unlimited" ]]; then
    return 0
  fi
  if ! is_uint "${soft}"; then
    return 0
  fi

  local soft_num="${soft}"
  local hard_num=-1
  if [[ "${hard}" == "unlimited" ]]; then
    hard_num=-1
  elif is_uint "${hard}"; then
    hard_num="${hard}"
  else
    hard_num="${soft_num}"
  fi

  if (( soft_num < required )); then
    local target="${required}"
    if (( hard_num >= 0 && target > hard_num )); then
      target="${hard_num}"
    fi
    if (( target > soft_num )); then
      ulimit -Sn "${target}" 2>/dev/null || true
      soft="$(ulimit -Sn 2>/dev/null || true)"
      if is_uint "${soft}"; then
        soft_num="${soft}"
      fi
    fi
  fi

  if (( soft_num >= required )); then
    return 0
  fi

  echo "Error: insufficient open-file limit for benchmark." >&2
  echo "  required soft nofile >= ${required} (clients*2+4096)" >&2
  echo "  current soft=${soft}, hard=${hard}" >&2
  if (( hard_num >= 0 && hard_num < required )); then
    echo "  hard limit is below required; raise hard limit first." >&2
  else
    echo "  try: ulimit -n ${required}" >&2
  fi
  echo "  or skip preflight with BENCH_SKIP_NOFILE_CHECK=1" >&2
  exit 2
}

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithzmq/multi_e2e/run_benchmarks.sh [options]

Run multi_e2e benchmark (server/client split) and compare libzmq vs zlink.

Options:
  --pattern NAME                Pattern (default: stream)
                                dealer_dealer, dealer_router,
                                router_router,
                                pubsub, stream
  --runs N                      Iterations per configuration (default: 5)
  --transport NAME              Transport list (default: tcp)
  --transports LIST             Alias for --transport
  --zlink-only                  Run only zlink (compare with cached libzmq)
  --refresh-std-cache           Refresh libzmq cache
  --std-cache-file PATH         Cache file path
  --result                      Write output to results/YYYYMMDD/
  --build-dir PATH              Forwarded to run_comparison.py
  --pin-cpu                     Forwarded to run_comparison.py
  --duration N                  BENCH_MULTI_DURATION_SECONDS (default: 5)
  --settle-ms N                 BENCH_MULTI_SETTLE_MS (default: 500)
  --clients N                   Override BENCH_MULTI_CLIENTS
  --inflight N                  BENCH_MULTI_INFLIGHT (default: 1)
  --drain-ms N                  BENCH_MULTI_DRAIN_MS (default: 300)
  --io-threads N                BENCH_IO_THREADS (default: 4)
  --msg-sizes LIST              BENCH_MSG_SIZES (e.g. 64,1024,65536)
  --run-cooldown-ms N           Sleep between runs (default: 3000)
  --help                        Show help
USAGE
}

RUNS=5
TRANSPORTS="${BENCH_TRANSPORTS:-${DEFAULT_TRANSPORT}}"
PATTERN_RAW="${DEFAULT_PATTERN}"
ZLINK_ONLY=0
REFRESH_STD_CACHE=0
STD_CACHE_FILE=""
RESULT_TO_FILE=0
BUILD_DIR=""
PIN_CPU=0
MSG_SIZES="${BENCH_MSG_SIZES:-}"
DURATION="${BENCH_MULTI_DURATION_SECONDS:-5}"
SETTLE_MS="${BENCH_MULTI_SETTLE_MS:-500}"
CLIENTS="${BENCH_MULTI_CLIENTS:-}"
INFLIGHT="${BENCH_MULTI_INFLIGHT:-1}"
DRAIN_MS="${BENCH_MULTI_DRAIN_MS:-300}"
IO_THREADS="${BENCH_IO_THREADS:-4}"
RUN_COOLDOWN_MS="${BENCH_MULTI_RUN_COOLDOWN_MS:-3000}"

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
      TRANSPORTS="${2:-}"
      shift 2
      ;;
    --zlink-only)
      ZLINK_ONLY=1
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
    --result)
      RESULT_TO_FILE=1
      shift
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --pin-cpu)
      PIN_CPU=1
      shift
      ;;
    --duration)
      DURATION="${2:-}"
      shift 2
      ;;
    --settle-ms)
      SETTLE_MS="${2:-}"
      shift 2
      ;;
    --clients)
      CLIENTS="${2:-}"
      shift 2
      ;;
    --inflight)
      INFLIGHT="${2:-}"
      shift 2
      ;;
    --drain-ms)
      DRAIN_MS="${2:-}"
      shift 2
      ;;
    --io-threads)
      IO_THREADS="${2:-}"
      shift 2
      ;;
    --msg-sizes)
      MSG_SIZES="${2:-}"
      shift 2
      ;;
    --run-cooldown-ms|--cooldown-ms)
      RUN_COOLDOWN_MS="${2:-}"
      shift 2
      ;;
    *)
      echo "Error: unknown option '$1'" >&2
      usage >&2
      exit 1
      ;;
  esac
done

PATTERN="$(normalize_pattern "${PATTERN_RAW}")" || {
  echo "Error: unsupported pattern '${PATTERN_RAW}'" >&2
  exit 1
}

if [[ -n "${CLIENTS}" ]]; then
  ensure_nofile_limit "${CLIENTS}"
else
  if [[ "${PATTERN}" == "MULTI_STREAM" ]]; then
    ensure_nofile_limit "10000"
  else
    ensure_nofile_limit "1000"
  fi
fi

export BENCH_TRANSPORTS="${TRANSPORTS}"
export BENCH_MULTI_DURATION_SECONDS="${DURATION}"
export BENCH_MULTI_SETTLE_MS="${SETTLE_MS}"
export BENCH_MULTI_INFLIGHT="${INFLIGHT}"
export BENCH_MULTI_DRAIN_MS="${DRAIN_MS}"
export BENCH_IO_THREADS="${IO_THREADS}"
export BENCH_MULTI_RUN_COOLDOWN_MS="${RUN_COOLDOWN_MS}"
if [[ -n "${MSG_SIZES}" ]]; then
  export BENCH_MSG_SIZES="${MSG_SIZES}"
fi
if [[ -n "${CLIENTS}" ]]; then
  export BENCH_MULTI_CLIENTS="${CLIENTS}"
fi

if [[ "${RESULT_TO_FILE}" -eq 1 ]]; then
  DATE_DIR="$(date +%Y%m%d)"
  TS="$(date +%Y%m%d_%H%M%S)"
  OUT_DIR="${RESULTS_ROOT}/${DATE_DIR}"
  mkdir -p "${OUT_DIR}"
  OUT_FILE="${OUT_DIR}/bench_multi_e2e_$(display_pattern "${PATTERN}")_${TS}.txt"
  exec > >(tee "${OUT_FILE}") 2>&1
  echo "Saving benchmark output to: ${OUT_FILE}"
fi

EXTRA_ARGS=("${PATTERN}" --runs "${RUNS}" --run-cooldown-ms "${RUN_COOLDOWN_MS}")
if [[ "${ZLINK_ONLY}" -eq 1 ]]; then
  EXTRA_ARGS+=(--zlink-only)
fi
if [[ "${REFRESH_STD_CACHE}" -eq 1 ]]; then
  EXTRA_ARGS+=(--refresh-std-cache)
fi
if [[ -n "${STD_CACHE_FILE}" ]]; then
  EXTRA_ARGS+=(--std-cache-file "${STD_CACHE_FILE}")
fi
if [[ -n "${BUILD_DIR}" ]]; then
  EXTRA_ARGS+=(--build-dir "${BUILD_DIR}")
fi
if [[ "${PIN_CPU}" -eq 1 ]]; then
  EXTRA_ARGS+=(--pin-cpu)
fi

python3 "${SCRIPT_DIR}/run_comparison.py" "${EXTRA_ARGS[@]}"
