#!/usr/bin/env bash
set -euo pipefail
set -o pipefail

# core/bench/benchwithzlink - zlink version comparison benchmarks
# Compares baseline (previous zlink) vs current (new build)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# repo root (three levels above: core/bench/benchwithzlink)
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

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
WITH_BASELINE=0
OUTPUT_FILE=""
RUNS=1
REUSE_BUILD=0
ZLINK_ONLY=0
PIN_CPU=0
BENCH_IO_THREADS=""
BENCH_MSG_SIZES=""
BENCH_TRANSPORTS=""
RESULTS=1
RESULTS_DIR=""
RESULTS_TAG=""
ALLOW_MULTI="${BENCH_ALLOW_MULTI:-0}"
BENCH_COMPARISON_SCRIPT="${BENCH_COMPARISON_SCRIPT:-${ROOT_DIR}/core/bench/benchwithzlink/run_comparison.py}"

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithzlink/run_benchmarks.sh [options]

Compare baseline zlink (previous version) vs current zlink (new build).
  Note: PATTERN=ALL (default) runs single-pattern benchmarks
  (PAIR/PUBSUB/DEALER/ROUTER/STREAM/GATEWAY/SPOT).
  Multi-socket benchmarks are excluded from this script.
  Use run_benchmarks_multi.sh for MULTI_* patterns.

Before running:
  1. Copy previous zlink library to core/bench/benchwithzlink/baseline/zlink_dist/<platform>-<arch>/
     - Linux: libzlink.so
     - macOS: libzlink.dylib
     - Windows: zlink.dll + zlink.lib

Options:
  -h, --help            Show this help.
  --with-baseline       Run baseline and refresh cache (default: use cache).
  --pattern NAME        Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER).
                       Use comma-separated patterns.
  --build-dir PATH      Build directory (default: core/build/<platform>-<arch>).
  --output PATH         Tee results to a file.
  --result              Write results under core/bench/benchwithzlink/results/YYYYMMDD/.
  --results-dir PATH    Override results root directory.
  --results-tag NAME    Optional tag appended to the results filename.
  --runs N              Iterations per configuration (default: 1).
  --zlink-only          Run only current zlink benchmarks (no baseline).
  --reuse-build         Reuse existing build dir without re-running CMake.
  --pin-cpu             Pin CPU core during benchmarks (Linux taskset).
  --io-threads N        Set BENCH_IO_THREADS for the benchmark run.
  --msg-sizes LIST      Comma-separated message sizes (e.g., 1024 or 64,1024,65536).
  --size N              Convenience alias for --msg-sizes N.
  --transports LIST     Comma-separated transports (e.g., tcp,tls,ws,wss).
  --transport NAME      Convenience alias for --transports NAME.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-baseline)
      WITH_BASELINE=1
      ;;
    --skip-libzlink)
      WITH_BASELINE=0
      ;;
    --with-libzlink)
      WITH_BASELINE=1
      ;;
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
    --result|--baseline)
      RESULTS=1
      ;;
    --results-dir|--baseline-dir)
      RESULTS_DIR="${2:-}"
      shift
      ;;
    --results-tag|--baseline-tag)
      RESULTS_TAG="${2:-}"
      shift
      ;;
    --runs)
      RUNS="${2:-}"
      shift
      ;;
    --zlink-only)
      ZLINK_ONLY=1
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
    --size)
      BENCH_MSG_SIZES="${2:-}"
      shift
      ;;
    --transports|--transport)
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

# Check baseline library exists when baseline run is requested
BASELINE_ROOT="${SCRIPT_DIR}/baseline/zlink_dist/${PLATFORM}-${ARCH}"
BASELINE_LIB_DIR="${BASELINE_ROOT}/lib"
BASELINE_BIN_DIR="${BASELINE_ROOT}/bin"
if [[ "${ZLINK_ONLY}" -eq 0 && "${WITH_BASELINE}" -eq 1 ]]; then
  if [[ ! -d "${BASELINE_ROOT}" ]]; then
    echo "Error: baseline directory not found: ${BASELINE_ROOT}" >&2
    echo "Please create core/bench/benchwithzlink/baseline/zlink_dist/${PLATFORM}-${ARCH} and copy previous zlink library there." >&2
    exit 1
  fi
  if [[ "${IS_WINDOWS}" -eq 1 ]]; then
    if [[ ! -d "${BASELINE_BIN_DIR}" || ! -d "${BASELINE_LIB_DIR}" ]]; then
      echo "Error: baseline bin/lib directories not found under ${BASELINE_ROOT}" >&2
      exit 1
    fi
    BASELINE_DLL_FILES=("${BASELINE_BIN_DIR}"/libzlink*.dll)
    BASELINE_LIB_FILES=("${BASELINE_LIB_DIR}"/libzlink*.lib)
    if [[ ! -e "${BASELINE_DLL_FILES[0]}" || ! -e "${BASELINE_LIB_FILES[0]}" ]]; then
      echo "Error: No baseline libzlink dll/lib found in ${BASELINE_ROOT}" >&2
      exit 1
    fi
  else
    if [[ ! -d "${BASELINE_LIB_DIR}" ]]; then
      echo "Error: baseline lib directory not found: ${BASELINE_LIB_DIR}" >&2
      exit 1
    fi
    BASELINE_LIB_FILES=("${BASELINE_LIB_DIR}"/libzlink.*)
    if [[ ! -e "${BASELINE_LIB_FILES[0]}" ]]; then
      echo "Error: No libzlink library found in ${BASELINE_LIB_DIR}" >&2
      echo "Please copy previous zlink library (libzlink.so/dylib) there." >&2
      exit 1
    fi
  fi
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

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  CMAKE_GENERATOR="${CMAKE_GENERATOR:-Visual Studio 17 2022}"
  CMAKE_ARCH="${CMAKE_ARCH:-x64}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -G "${CMAKE_GENERATOR}" \
    -A "${CMAKE_ARCH}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BENCHMARKS=ON \
    -DZLINK_BUILD_BENCH_ZMQ=OFF \
    -DZLINK_BUILD_BENCH_ZLINK=ON \
    -DZLINK_BUILD_BENCH_BEAST=OFF \
    -DZLINK_CXX_STANDARD=17
else
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BENCHMARKS=ON \
    -DZLINK_BUILD_BENCH_ZMQ=OFF \
    -DZLINK_BUILD_BENCH_ZLINK=ON \
    -DZLINK_BUILD_BENCH_BEAST=OFF \
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
if [[ "${ZLINK_ONLY}" -eq 1 ]]; then
  RUN_CMD_BASE+=(--zlink-only)
else
  if [[ "${WITH_BASELINE}" -eq 1 ]]; then
    RUN_CMD_BASE+=(--refresh-libzlink)
  else
    CACHE_FILE="${ROOT_DIR}/core/bench/benchwithzlink/baseline_cache_${PLATFORM}-${ARCH}.json"
    if [[ ! -f "${CACHE_FILE}" ]]; then
      echo "Baseline cache not found: ${CACHE_FILE}" >&2
      echo "Run with --with-baseline once to generate the baseline." >&2
      exit 1
    fi
  fi
fi

RUN_CMD=("${RUN_CMD_BASE[@]}")
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
