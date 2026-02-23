#!/usr/bin/env bash
set -euo pipefail
set -o pipefail

# core/perf - zlink benchmark runner
# Default mode measures current zlink only.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# repo root (two levels above: core/perf)
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

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

IS_WINDOWS=0
PLATFORM="linux"
ARCH="x64"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    IS_WINDOWS=1
    PLATFORM="windows"
    ;;
  Darwin*)
    PLATFORM="macos"
    ;;
  Linux*)
    PLATFORM="linux"
    ;;
esac

case "$(uname -m)" in
  x86_64|amd64)
    ARCH="x64"
    ;;
  aarch64|arm64)
    ARCH="arm64"
    ;;
  *)
    ARCH="$(uname -m)"
    ;;
esac

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  BUILD_DIR="${ROOT_DIR}/core/build/windows-x64"
else
  BUILD_DIR="${ROOT_DIR}/core/build/${PLATFORM}-${ARCH}"
fi
STANDARD_PATTERNS="PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,ROUTER_ROUTER_POLL,STREAM,GATEWAY,SPOT"
PATTERN="ALL"
OUTPUT_FILE=""
RUNS=1
REUSE_BUILD=0
PIN_CPU=0
BENCH_IO_THREADS=""
BENCH_MSG_SIZES=""
BENCH_TRANSPORTS=""
RESULTS=1
RESULTS_DIR=""
RESULTS_TAG=""
ALLOW_MULTI="${BENCH_ALLOW_MULTI:-0}"
BENCH_COMPARISON_SCRIPT="${BENCH_COMPARISON_SCRIPT:-${SCRIPT_DIR}/run_comparison.py}"

usage() {
  cat <<'USAGE'
Usage: core/perf/run_benchmarks.sh [options]

Measure current zlink performance.
  Note: PATTERN=ALL (default) runs single-pattern benchmarks
  (PAIR/PUBSUB/DEALER/ROUTER/STREAM/GATEWAY/SPOT).
  Multi-socket benchmarks are excluded from this script.
  Use run_benchmarks_multi.sh for MULTI_* patterns.

Options:
  -h, --help            Show this help.
  --pattern NAME        Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER).
                       Use comma-separated patterns.
  --build-dir PATH      Build directory (default: core/build/<platform>-<arch>).
  --output PATH         Tee results to a file.
  --result              Write results under core/perf/results/YYYYMMDD/.
  --results-dir PATH    Override results root directory.
  --results-tag NAME    Optional tag appended to the results filename.
  --runs N              Iterations per configuration (default: 1).
  --reuse-build         Reuse existing build dir without re-running CMake.
  --pin-cpu             Pin CPU core during benchmarks (Linux taskset).
  --io-threads N        Set BENCH_IO_THREADS for the benchmark run.
  --msg-sizes LIST      Comma-separated message sizes (e.g., 1024 or 64,1024,65536).
  --transports LIST     Comma-separated transports (e.g., tcp,tls,ws,wss).
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern)
      PATTERN="${2:-}"
      shift
      ;;
    --reuse-build)
      REUSE_BUILD=1
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift
      ;;
    --output)
      OUTPUT_FILE="${2:-}"
      shift
      ;;
    --result)
      RESULTS=1
      ;;
    --results-dir)
      RESULTS_DIR="${2:-}"
      shift
      ;;
    --results-tag)
      RESULTS_TAG="${2:-}"
      shift
      ;;
    --runs)
      RUNS="${2:-}"
      shift
      ;;
    --pin-cpu)
      PIN_CPU=1
      ;;
    --io-threads)
      BENCH_IO_THREADS="${2:-}"
      shift
      ;;
    --msg-sizes)
      BENCH_MSG_SIZES="${2:-}"
      shift
      ;;
    --transports)
      BENCH_TRANSPORTS="${2:-}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      if [[ "$1" != --* ]]; then
        if [[ -z "${PATTERN}" || "${PATTERN}" == "ALL" ]]; then
          PATTERN="$1"
        else
          PATTERN="${PATTERN},$1"
        fi
      else
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
      fi
      ;;
  esac
  shift
done

if [[ -z "${PATTERN}" ]]; then
  echo "Pattern name is required." >&2
  usage >&2
  exit 1
fi

if [[ ! -f "${BENCH_COMPARISON_SCRIPT}" ]]; then
  echo "Error: comparison script not found: ${BENCH_COMPARISON_SCRIPT}" >&2
  exit 1
fi

# Normalize pattern list
if [[ "${PATTERN}" != "ALL" ]]; then
  PATTERN="$(printf '%s' "${PATTERN}" | tr '[:lower:]' '[:upper:]')"
else
  PATTERN="${STANDARD_PATTERNS}"
fi

IFS=',' read -r -a PATTERN_LIST <<< "${PATTERN}"
if [[ "${#PATTERN_LIST[@]}" -eq 0 ]]; then
  echo "Error: no valid pattern specified." >&2
  exit 1
fi

for i in "${!PATTERN_LIST[@]}"; do
  PATTERN_LIST[i]="${PATTERN_LIST[i]//[[:space:]]/}"
  if [[ -z "${PATTERN_LIST[i]}" ]]; then
    echo "Error: empty pattern entry in list." >&2
    exit 1
  fi
done

for p in "${PATTERN_LIST[@]}"; do
  if [[ "${p}" == *"MULTI_"* && "${ALLOW_MULTI}" -ne 1 ]]; then
    echo "Error: run_benchmarks.sh is single-pattern mode only." >&2
    echo "Use run_benchmarks_multi.sh for MULTI_* patterns." >&2
    exit 1
  fi
done

if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_IO_THREADS}" && ! "${BENCH_IO_THREADS}" =~ ^[0-9]+$ ]]; then
  echo "BENCH_IO_THREADS must be a positive integer." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_MSG_SIZES}" && ! "${BENCH_MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "BENCH_MSG_SIZES must be a comma-separated list of integers." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_TRANSPORTS}" && ! "${BENCH_TRANSPORTS}" =~ ^[a-z]+(,[a-z]+)*$ ]]; then
  echo "BENCH_TRANSPORTS must be a comma-separated list of names." >&2
  usage >&2
  exit 1
fi

if [[ "${RESULTS}" -eq 1 ]]; then
  if [[ -n "${OUTPUT_FILE}" ]]; then
    echo "Error: --result cannot be used with --output." >&2
    exit 1
  fi
  if [[ -z "${RESULTS_DIR}" ]]; then
    RESULTS_DIR="${SCRIPT_DIR}/results"
  fi
  DATE_DIR="$(date +%Y%m%d)"
  TS="$(date +%Y%m%d_%H%M%S)"
  NAME="bench_${PLATFORM}_${TS}"
  if [[ -n "${RESULTS_TAG}" ]]; then
    NAME="${NAME}_${RESULTS_TAG}"
  fi
  OUTPUT_FILE="${RESULTS_DIR}/${DATE_DIR}/${NAME}.txt"
fi

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
ROOT_DIR="$(realpath -m "${ROOT_DIR}")"

if [[ "${BUILD_DIR}" != "${ROOT_DIR}/"* ]]; then
  echo "Build directory must be inside repo root: ${ROOT_DIR}" >&2
  exit 1
fi

if [[ "${REUSE_BUILD}" -eq 1 ]]; then
  if [[ -d "${BUILD_DIR}" ]]; then
    echo "Reusing build directory: ${BUILD_DIR}"
  else
    echo "Build directory not found. Creating: ${BUILD_DIR}"
  fi
else
  echo "Cleaning build directory: ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

CMAKE_SOURCE_DIR="${ROOT_DIR}"
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  CACHE_CMAKE_SOURCE="$(
    sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" \
      | tail -n 1
  )"
  if [[ -n "${CACHE_CMAKE_SOURCE}" && "${CACHE_CMAKE_SOURCE}" != "${CMAKE_SOURCE_DIR}" ]]; then
    echo "Build cache source mismatch detected:"
    echo "  cache source: ${CACHE_CMAKE_SOURCE}"
    echo "  required source: ${CMAKE_SOURCE_DIR}"
    echo "Resetting build directory: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
  fi
fi
echo "Using CMake source directory: ${CMAKE_SOURCE_DIR}"

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  CMAKE_GENERATOR="${CMAKE_GENERATOR:-Visual Studio 17 2022}"
  CMAKE_ARCH="${CMAKE_ARCH:-x64}"
  cmake -S "${CMAKE_SOURCE_DIR}" -B "${BUILD_DIR}" \
    -G "${CMAKE_GENERATOR}" \
    -A "${CMAKE_ARCH}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BENCHMARKS=ON \
    -DZLINK_BUILD_TESTS=OFF \
    -DZLINK_BUILD_BENCH_ZMQ=OFF \
    -DZLINK_BUILD_BENCH_ZLINK=ON \
    -DZLINK_BUILD_BENCH_BEAST=OFF \
    -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF \
    -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF \
    -DZLINK_CXX_STANDARD=17
else
  cmake -S "${CMAKE_SOURCE_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BENCHMARKS=ON \
    -DZLINK_BUILD_TESTS=OFF \
    -DZLINK_BUILD_BENCH_ZMQ=OFF \
    -DZLINK_BUILD_BENCH_ZLINK=ON \
    -DZLINK_BUILD_BENCH_BEAST=OFF \
    -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF \
    -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF \
    -DZLINK_CXX_STANDARD=17
fi

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  cmake --build "${BUILD_DIR}" --config Release
else
  cmake --build "${BUILD_DIR}"
fi

PYTHON_BIN=()
if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  if command -v py >/dev/null 2>&1; then
    PYTHON_BIN=(py -3)
  elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=(python)
  elif command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN=(python3)
  else
    echo "Python not found. Install Python 3 or ensure it is on PATH." >&2
    exit 1
  fi
else
  if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN=(python3)
  elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=(python)
  else
    echo "Python not found. Install Python 3 or ensure it is on PATH." >&2
    exit 1
  fi
fi

RUN_CMD_BASE=("${PYTHON_BIN[@]}" "${BENCH_COMPARISON_SCRIPT}" --build-dir "${BUILD_DIR}" --runs "${RUNS}")
RUN_ENV=()
if [[ -n "${BENCH_IO_THREADS}" ]]; then
  RUN_ENV+=(BENCH_IO_THREADS="${BENCH_IO_THREADS}")
fi
if [[ -n "${BENCH_MSG_SIZES}" ]]; then
  RUN_ENV+=(BENCH_MSG_SIZES="${BENCH_MSG_SIZES}")
fi
if [[ -n "${BENCH_TRANSPORTS}" ]]; then
  RUN_ENV+=(BENCH_TRANSPORTS="${BENCH_TRANSPORTS}")
fi
if [[ "${PIN_CPU}" -eq 1 ]]; then
  RUN_CMD_BASE+=(--pin-cpu)
fi

RUN_CMD=("${RUN_CMD_BASE[@]}")
SHOW_TOTAL_TIME=1
if [[ "${#PATTERN_LIST[@]}" -eq 1 ]]; then
  if [[ -n "${OUTPUT_FILE}" ]]; then
    mkdir -p "$(dirname "${OUTPUT_FILE}")"
    env "${RUN_ENV[@]}" "${RUN_CMD[@]}" "${PATTERN_LIST[0]}" | tee "${OUTPUT_FILE}"
  else
    env "${RUN_ENV[@]}" "${RUN_CMD[@]}" "${PATTERN_LIST[0]}"
  fi
else
  for p in "${PATTERN_LIST[@]}"; do
    if [[ -n "${OUTPUT_FILE}" ]]; then
      SAFE_PATTERN="${p//,/\\_}"
      PATTERN_OUTPUT="${RESULTS_DIR}/${DATE_DIR}/bench_${PLATFORM}_${SAFE_PATTERN}_${TS}"
      if [[ -n "${RESULTS_TAG}" ]]; then
        PATTERN_OUTPUT="${PATTERN_OUTPUT}_${RESULTS_TAG}"
      fi
      PATTERN_OUTPUT="${PATTERN_OUTPUT}.txt"
      mkdir -p "$(dirname "${PATTERN_OUTPUT}")"
      env "${RUN_ENV[@]}" "${RUN_CMD[@]}" "${p}" | tee "${PATTERN_OUTPUT}"
    else
      env "${RUN_ENV[@]}" "${RUN_CMD[@]}" "${p}"
    fi
  done
fi
