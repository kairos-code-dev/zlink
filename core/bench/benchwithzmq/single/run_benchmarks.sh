#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)/results"

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
Usage: core/bench/benchwithzmq/single/run_benchmarks.sh [options]

Run SINGLE benchmark patterns and compare libzmq vs zlink.

Options:
  --patterns LIST               Pattern list (default: ALL)
                                e.g. pair,pubsub,dealer_dealer,router_router
  --pattern NAME                Alias for one pattern
  --runs N                      Iterations per configuration (default: 1)
  --transport LIST              Transport list (default: tcp,inproc,ipc)
  --transports LIST             Alias for --transport
  --msg-sizes LIST              Override BENCH_MSG_SIZES (e.g. 64,1024,65536)
  --build-dir PATH              Forwarded to run_comparison.py
  --pin-cpu                     Forwarded to run_comparison.py
  --result                      Write output to core/bench/benchwithzmq/results/YYYYMMDD/
  --help                        Show help
USAGE
}

PATTERNS="ALL"
RUNS=1
TRANSPORTS="${BENCH_TRANSPORTS:-tcp,inproc,ipc}"
MSG_SIZES="${BENCH_MSG_SIZES:-}"
BUILD_DIR=""
PIN_CPU=0
RESULT_TO_FILE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --patterns)
      PATTERNS="${2:-}"
      shift 2
      ;;
    --pattern)
      PATTERNS="${2:-}"
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
    --msg-sizes)
      MSG_SIZES="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --pin-cpu)
      PIN_CPU=1
      shift
      ;;
    --result)
      RESULT_TO_FILE=1
      shift
      ;;
    *)
      echo "Error: unknown option '$1'" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -n "${TRANSPORTS}" ]]; then
  export BENCH_TRANSPORTS="${TRANSPORTS}"
fi
if [[ -n "${MSG_SIZES}" ]]; then
  export BENCH_MSG_SIZES="${MSG_SIZES}"
fi

PATTERN_UP="$(echo "${PATTERNS}" | tr '[:lower:]' '[:upper:]' | tr -d ' ')"
if [[ "${PATTERN_UP}" == "STREAM" ]]; then
  if [[ "${TRANSPORTS}" != "tcp" ]]; then
    echo "Error: benchwithzmq STREAM supports only tcp transport." >&2
    exit 1
  fi
  if [[ -z "${BENCH_STREAM_HWM:-}" ]]; then
    export BENCH_STREAM_HWM="100000"
  fi
  if [[ -z "${BENCH_STREAM_SERVER_IO_THREADS:-}" ]]; then
    export BENCH_STREAM_SERVER_IO_THREADS="4"
  fi
  if [[ -z "${BENCH_IO_THREADS:-}" ]]; then
    export BENCH_IO_THREADS="4"
  fi
fi

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

  TAG="$(echo "${PATTERNS}" | tr '[:lower:], ' '[:upper:]__')"
  OUTPUT_DIR="${RESULTS_ROOT}/${DATE_DIR}"
  mkdir -p "${OUTPUT_DIR}"
  OUTPUT_FILE="${OUTPUT_DIR}/bench_${PLATFORM}_${TAG}_${TS}.txt"
  exec > >(tee "${OUTPUT_FILE}") 2>&1
  echo "Saving benchmark output to: ${OUTPUT_FILE}"
fi

EXTRA_ARGS=(--patterns "${PATTERNS}" --runs "${RUNS}")
if [[ -n "${BUILD_DIR}" ]]; then
  EXTRA_ARGS+=(--build-dir "${BUILD_DIR}")
fi
if [[ "${PIN_CPU}" -eq 1 ]]; then
  EXTRA_ARGS+=(--pin-cpu)
fi

SHOW_TOTAL_TIME=1
python3 "${SCRIPT_DIR}/run_comparison.py" "${EXTRA_ARGS[@]}"
