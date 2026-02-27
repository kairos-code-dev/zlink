#!/usr/bin/env bash
set -euo pipefail
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
  if [[ "${PERF_SUPPRESS_TOTAL_TIME:-0}" == "1" ]]; then
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
  if [[ -d "${ROOT_DIR}/core/build/bin" ]]; then
    BUILD_DIR="${ROOT_DIR}/core/build"
  else
    BUILD_DIR="${ROOT_DIR}/core/build/${PLATFORM}-${ARCH}"
  fi
fi

STANDARD_PATTERNS="PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,ROUTER_ROUTER_POLL,GATEWAY,SPOT"
PATTERN="ALL"
OUTPUT_FILE=""
SAVE_REPORT=1
SAVE_BASELINE=0
SAVE_BASELINE_VERSION=""
RESULTS_DIR=""
RESULTS_TAG=""
RESULT_FILE=""
RUNS=""
RUNS_EXPLICIT=0
BUILD_MODE="incremental"
BUILD_MODE_EXPLICIT=0
PIN_CPU=0
PERF_IO_THREADS="${PERF_IO_THREADS:-}"
PERF_MSG_SIZES="${PERF_MSG_SIZES:-}"
PERF_TRANSPORTS="${PERF_TRANSPORTS:-}"
SINGLE_DURATION_SECONDS="${PERF_SINGLE_DURATION_SECONDS:-5}"
SINGLE_HWM="${PERF_SINGLE_HWM:-}"
SINGLE_SNDHWM="${PERF_SINGLE_SNDHWM:-}"
SINGLE_RCVHWM="${PERF_SINGLE_RCVHWM:-}"
SINGLE_SNDTIMEO_MS="${PERF_SINGLE_SNDTIMEO_MS:-}"
SINGLE_RCVTIMEO_MS="${PERF_SINGLE_RCVTIMEO_MS:-${PERF_SINGLE_PUBSUB_RCVTIMEO_MS:-}}"
MODE="observe"
BASELINE_FILE=""
ROLLING_N="${PERF_ROLLING_N:-10}"
PERF_ALLOW_MULTI="${PERF_ALLOW_MULTI:-0}"
if [[ "${PERF_ALLOW_MULTI}" == "1" ]]; then
  PERF_COMPARISON_SCRIPT="${SCRIPT_DIR}/run_comparison.py"
else
  PERF_COMPARISON_SCRIPT="${SCRIPT_DIR}/single/run_comparison.py"
fi

usage() {
  cat <<'USAGE'
Usage: core/perf/run_benchmarks.sh [options]

Measure current zlink single-pattern performance.

Options:
  -h, --help                  Show this help.
  --pattern NAME              Pattern list (comma-separated) or ALL.
  --build-dir PATH            Build directory (default: core/build/<platform>-<arch>).
  --reuse-build               Reuse existing build directory as-is (skip configure/build).
  --clean-build               Remove build directory and do a clean build.
  --output PATH               Tee console logs to a file.
  --save [VERSION]            Save baseline under results/single/baseline/ (complete only).
  --results-dir PATH          Override result root directory.
  --results-tag NAME          Optional tag in saved result filename.
  --runs N                    Iterations per pattern/transport/size (default by mode: observe=1, trend=3, gate=5).
  --duration N                Override single duration seconds (default: 5).
  --hwm N                     Override PERF_SINGLE_HWM (default: 1000 in binary).
  --send-hwm N                Override PERF_SINGLE_SNDHWM (fallback: --hwm).
  --recv-hwm N                Override PERF_SINGLE_RCVHWM (fallback: --hwm).
  --sndtimeo N                Override PERF_SINGLE_SNDTIMEO_MS (default: 200).
  --rcvtimeo N                Override PERF_SINGLE_RCVTIMEO_MS (default: 200).
  --pin-cpu                   Pin CPU core during benchmark runs (Linux taskset).
  --io-threads N              Set PERF_IO_THREADS for benchmark binaries.
  --msg-sizes LIST            Comma-separated sizes (e.g., 64,1024,65536).
  --transports LIST           Comma-separated transports.

Policy options (single runner):
  --mode MODE                 observe|trend|gate (default: observe).
  --baseline-file PATH        Baseline file for gate mode.
  --rolling-n N               Rolling baseline window for trend mode (default: 10).

Notes:
  - tmp result is always saved under results/single/tmp/.
  - report result save is always enabled.
  - default build mode is incremental (configure/build without deleting build dir).
  - --output and report save can be used together.
  - --save-baseline is removed. Use --save [VERSION].
  - MULTI_* patterns are rejected by this script.
USAGE
}

set_build_mode() {
  local next_mode="${1:-}"
  if [[ "${next_mode}" != "incremental" && "${next_mode}" != "reuse" && "${next_mode}" != "clean" ]]; then
    echo "Error: invalid build mode: ${next_mode}" >&2
    exit 1
  fi
  if [[ "${BUILD_MODE_EXPLICIT}" -eq 1 && "${BUILD_MODE}" != "${next_mode}" ]]; then
    echo "Error: --reuse-build and --clean-build are mutually exclusive." >&2
    exit 1
  fi
  BUILD_MODE="${next_mode}"
  BUILD_MODE_EXPLICIT=1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern)
      PATTERN="${2:-}"
      shift
      ;;
    --reuse-build)
      set_build_mode "reuse"
      ;;
    --clean-build)
      set_build_mode "clean"
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift
      ;;
    --output)
      OUTPUT_FILE="${2:-}"
      shift
      ;;
    --save)
      SAVE_BASELINE=1
      if [[ $# -ge 2 && "${2:-}" != --* ]]; then
        SAVE_BASELINE_VERSION="${2:-}"
        shift
      fi
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
      RUNS_EXPLICIT=1
      shift
      ;;
    --duration)
      SINGLE_DURATION_SECONDS="${2:-}"
      shift
      ;;
    --hwm)
      SINGLE_HWM="${2:-}"
      shift
      ;;
    --send-hwm)
      SINGLE_SNDHWM="${2:-}"
      shift
      ;;
    --recv-hwm)
      SINGLE_RCVHWM="${2:-}"
      shift
      ;;
    --sndtimeo|--sndtimeo-ms)
      SINGLE_SNDTIMEO_MS="${2:-}"
      shift
      ;;
    --rcvtimeo|--rcvtimeo-ms)
      SINGLE_RCVTIMEO_MS="${2:-}"
      shift
      ;;
    --pin-cpu)
      PIN_CPU=1
      ;;
    --io-threads)
      PERF_IO_THREADS="${2:-}"
      shift
      ;;
    --msg-sizes)
      PERF_MSG_SIZES="${2:-}"
      shift
      ;;
    --transports)
      PERF_TRANSPORTS="${2:-}"
      shift
      ;;
    --mode)
      MODE="${2:-}"
      shift
      ;;
    --baseline-file)
      BASELINE_FILE="${2:-}"
      shift
      ;;
    --rolling-n)
      ROLLING_N="${2:-}"
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

MULTI_PATTERN_COUNT=0
SINGLE_PATTERN_COUNT=0
for p in "${PATTERN_LIST[@]}"; do
  if [[ "${p}" == MULTI_* ]]; then
    MULTI_PATTERN_COUNT=$((MULTI_PATTERN_COUNT + 1))
    if [[ "${PERF_ALLOW_MULTI}" != "1" ]]; then
      echo "Error: run_benchmarks.sh is single-pattern mode only." >&2
      echo "Use run_benchmarks_multi.sh for MULTI_* patterns." >&2
      exit 1
    fi
  else
    SINGLE_PATTERN_COUNT=$((SINGLE_PATTERN_COUNT + 1))
  fi
done

if (( MULTI_PATTERN_COUNT > 0 && SINGLE_PATTERN_COUNT > 0 )); then
  echo "Error: cannot mix single and multi patterns in one run." >&2
  exit 1
fi

if [[ ! -f "${PERF_COMPARISON_SCRIPT}" ]]; then
  echo "Error: comparison script not found: ${PERF_COMPARISON_SCRIPT}" >&2
  exit 1
fi

if [[ -n "${PERF_IO_THREADS}" && ! "${PERF_IO_THREADS}" =~ ^[0-9]+$ ]]; then
  echo "PERF_IO_THREADS must be a non-negative integer." >&2
  exit 1
fi

if [[ -n "${PERF_MSG_SIZES}" && ! "${PERF_MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "PERF_MSG_SIZES must be a comma-separated list of integers." >&2
  exit 1
fi

if [[ -n "${PERF_TRANSPORTS}" && ! "${PERF_TRANSPORTS}" =~ ^[a-z]+(,[a-z]+)*$ ]]; then
  echo "PERF_TRANSPORTS must be a comma-separated list of names." >&2
  exit 1
fi

if [[ -n "${SINGLE_DURATION_SECONDS}" && ( ! "${SINGLE_DURATION_SECONDS}" =~ ^[0-9]+$ || "${SINGLE_DURATION_SECONDS}" -lt 1 ) ]]; then
  echo "duration must be a positive integer." >&2
  exit 1
fi
if [[ -n "${SINGLE_HWM}" && ( ! "${SINGLE_HWM}" =~ ^[0-9]+$ || "${SINGLE_HWM}" -lt 1 ) ]]; then
  echo "hwm must be a positive integer." >&2
  exit 1
fi
if [[ -n "${SINGLE_SNDHWM}" && ( ! "${SINGLE_SNDHWM}" =~ ^[0-9]+$ || "${SINGLE_SNDHWM}" -lt 1 ) ]]; then
  echo "send-hwm must be a positive integer." >&2
  exit 1
fi
if [[ -n "${SINGLE_RCVHWM}" && ( ! "${SINGLE_RCVHWM}" =~ ^[0-9]+$ || "${SINGLE_RCVHWM}" -lt 1 ) ]]; then
  echo "recv-hwm must be a positive integer." >&2
  exit 1
fi
if [[ -n "${SINGLE_SNDTIMEO_MS}" && ( ! "${SINGLE_SNDTIMEO_MS}" =~ ^[0-9]+$ || "${SINGLE_SNDTIMEO_MS}" -lt 1 ) ]]; then
  echo "sndtimeo must be a positive integer." >&2
  exit 1
fi
if [[ -n "${SINGLE_RCVTIMEO_MS}" && ( ! "${SINGLE_RCVTIMEO_MS}" =~ ^[0-9]+$ || "${SINGLE_RCVTIMEO_MS}" -lt 1 ) ]]; then
  echo "rcvtimeo must be a positive integer." >&2
  exit 1
fi

MODE="$(printf '%s' "${MODE}" | tr '[:upper:]' '[:lower:]')"
if [[ "${MODE}" != "observe" && "${MODE}" != "trend" && "${MODE}" != "gate" ]]; then
  echo "Mode must be one of: observe, trend, gate." >&2
  exit 1
fi

if [[ "${RUNS_EXPLICIT}" -eq 0 ]]; then
  case "${MODE}" in
    observe) RUNS=1 ;;
    trend) RUNS=3 ;;
    gate) RUNS=5 ;;
  esac
fi
if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  exit 1
fi

if [[ -z "${ROLLING_N}" || ! "${ROLLING_N}" =~ ^[0-9]+$ || "${ROLLING_N}" -lt 1 ]]; then
  echo "rolling-n must be a positive integer." >&2
  exit 1
fi

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
ROOT_DIR="$(realpath -m "${ROOT_DIR}")"
PERF_COMPARISON_SCRIPT="$(realpath -m "${PERF_COMPARISON_SCRIPT}")"

if [[ "${BUILD_DIR}" != "${ROOT_DIR}/"* ]]; then
  echo "Build directory must be inside repo root: ${ROOT_DIR}" >&2
  exit 1
fi

if [[ -z "${RESULTS_DIR}" ]]; then
  RESULTS_DIR="${SCRIPT_DIR}/results"
fi
if [[ -n "${RESULTS_DIR}" ]]; then
  RESULTS_DIR="$(realpath -m "${RESULTS_DIR}")"
fi

TS="$(date +%Y%m%d_%H%M%S)"
NAME="perf_${PLATFORM}_${TS}"
if [[ -n "${RESULTS_TAG}" ]]; then
  NAME="${NAME}_${RESULTS_TAG}"
fi
RESULT_SUITE="single"
if (( MULTI_PATTERN_COUNT > 0 )); then
  RESULT_SUITE="multi"
fi
RESULT_FILE="${RESULTS_DIR}/${RESULT_SUITE}/tmp/${NAME}.txt"

if [[ -n "${OUTPUT_FILE}" ]]; then
  OUTPUT_FILE="$(realpath -m "${OUTPUT_FILE}")"
fi

if [[ -n "${RESULT_FILE}" && -n "${OUTPUT_FILE}" && "${RESULT_FILE}" == "${OUTPUT_FILE}" ]]; then
  echo "Error: --output cannot point to the same file as report result output." >&2
  exit 1
fi

cleanup_old_results_dirs() {
  local root="${1:-}"
  local retention="${PERF_RESULTS_RETENTION_DAYS:-90}"
  if [[ -z "${root}" || ! -d "${root}" ]]; then
    return
  fi
  if [[ -z "${retention}" || ! "${retention}" =~ ^[0-9]+$ || "${retention}" -le 0 ]]; then
    return
  fi

  local cutoff
  cutoff="$(date -u -d "-${retention} days" +%Y%m%d 2>/dev/null || true)"
  if [[ -z "${cutoff}" ]]; then
    return
  fi

  local dir base
  for dir in "${root}"/*; do
    [[ -d "${dir}" ]] || continue
    base="$(basename "${dir}")"
    if [[ ! "${base}" =~ ^[0-9]{8}$ ]]; then
      continue
    fi
    if [[ "${base}" < "${cutoff}" ]]; then
      rm -rf "${dir}"
    fi
  done
}

case "${BUILD_MODE}" in
  reuse)
    if [[ ! -d "${BUILD_DIR}" ]]; then
      echo "Error: --reuse-build requires an existing build directory: ${BUILD_DIR}" >&2
      exit 1
    fi
    echo "Reusing build directory: ${BUILD_DIR}"
    ;;
  clean)
    echo "Cleaning build directory: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
    ;;
  incremental)
    if [[ -d "${BUILD_DIR}" ]]; then
      echo "Using incremental build directory: ${BUILD_DIR}"
    else
      echo "Creating build directory: ${BUILD_DIR}"
    fi
    ;;
  *)
    echo "Error: invalid build mode: ${BUILD_MODE}" >&2
    exit 1
    ;;
esac

CMAKE_SOURCE_DIR="${ROOT_DIR}"
if [[ "${BUILD_MODE}" != "reuse" && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
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

if [[ "${BUILD_MODE}" != "reuse" ]]; then
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

if [[ -n "${RESULTS_DIR}" ]]; then
  cleanup_old_results_dirs "${RESULTS_DIR}"
fi

PATTERN_CSV="$(IFS=,; echo "${PATTERN_LIST[*]}")"
RUN_CMD=("${PYTHON_BIN[@]}" "${PERF_COMPARISON_SCRIPT}" "${PATTERN_CSV}" "--build-dir" "${BUILD_DIR}" "--runs" "${RUNS}")
if [[ "${PIN_CPU}" -eq 1 ]]; then
  RUN_CMD+=("--pin-cpu")
fi
if [[ -n "${SINGLE_DURATION_SECONDS}" ]]; then
  RUN_CMD+=("--duration" "${SINGLE_DURATION_SECONDS}")
fi

RUN_CMD+=("--mode" "${MODE}" "--rolling-n" "${ROLLING_N}")
if [[ -n "${RESULTS_DIR}" ]]; then
  RUN_CMD+=("--results-dir" "${RESULTS_DIR}")
fi
if [[ -n "${BASELINE_FILE}" ]]; then
  RUN_CMD+=("--baseline-file" "${BASELINE_FILE}")
fi
if [[ -n "${RESULTS_TAG}" ]]; then
  RUN_CMD+=("--results-tag" "${RESULTS_TAG}")
fi
RUN_CMD+=("--result-file" "${RESULT_FILE}")
if [[ "${SAVE_REPORT}" -eq 1 ]]; then
  RUN_CMD+=("--result")
fi
if [[ "${SAVE_BASELINE}" -eq 1 ]]; then
  RUN_CMD+=("--save")
  if [[ -n "${SAVE_BASELINE_VERSION}" ]]; then
    RUN_CMD+=("${SAVE_BASELINE_VERSION}")
  fi
fi

RUN_ENV=()
RUN_ENV+=(PYTHONUNBUFFERED=1)
if [[ -n "${PERF_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_IO_THREADS="${PERF_IO_THREADS}")
fi
if [[ -n "${PERF_MSG_SIZES}" ]]; then
  RUN_ENV+=(PERF_MSG_SIZES="${PERF_MSG_SIZES}")
fi
if [[ -n "${PERF_TRANSPORTS}" ]]; then
  RUN_ENV+=(PERF_TRANSPORTS="${PERF_TRANSPORTS}")
fi
if [[ -n "${SINGLE_DURATION_SECONDS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_DURATION_SECONDS="${SINGLE_DURATION_SECONDS}")
fi
if [[ -n "${SINGLE_HWM}" ]]; then
  RUN_ENV+=(PERF_SINGLE_HWM="${SINGLE_HWM}")
fi
if [[ -n "${SINGLE_SNDHWM}" ]]; then
  RUN_ENV+=(PERF_SINGLE_SNDHWM="${SINGLE_SNDHWM}")
fi
if [[ -n "${SINGLE_RCVHWM}" ]]; then
  RUN_ENV+=(PERF_SINGLE_RCVHWM="${SINGLE_RCVHWM}")
fi
if [[ -n "${SINGLE_SNDTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_SNDTIMEO_MS="${SINGLE_SNDTIMEO_MS}")
fi
if [[ -n "${SINGLE_RCVTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_RCVTIMEO_MS="${SINGLE_RCVTIMEO_MS}")
fi
if [[ "${BUILD_MODE}" == "reuse" ]]; then
  RUN_ENV+=(PERF_NO_AUTOBUILD=1)
fi
if [[ -n "${PERF_DISABLE_RESOURCE_METRICS:-}" ]]; then
  RUN_ENV+=(PERF_DISABLE_RESOURCE_METRICS="${PERF_DISABLE_RESOURCE_METRICS}")
fi

SHOW_TOTAL_TIME=1
if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}" | tee "${OUTPUT_FILE}"
else
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}"
fi
