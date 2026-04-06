#!/usr/bin/env bash
set -euo pipefail
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
CPP_PERF_DIR="${ROOT_DIR}/bindings/cpp/perf"
CPP_RUNTIME_ROOT="${CPP_PERF_DIR}/.runtime"
NORMALIZE_TIMESTAMPS_SH="${ROOT_DIR}/core/tools/normalize_build_timestamps.sh"
MAKE_BIN="$(command -v gmake || command -v make)"

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

BUILD_DIR="${ROOT_DIR}/core/build"

STANDARD_PATTERNS="PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT"
MULTI_PATTERNS="DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,SPOT,STREAM"
PATTERN="ALL"
OUTPUT_FILE=""
RESULTS_DIR="${CPP_PERF_DIR}/results"
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
RECV_MODE="${PERF_RECV_MODE:-callback}"
SINGLE_DURATION_SECONDS="${PERF_SINGLE_DURATION_SECONDS:-5}"
SINGLE_WARMUP_SECONDS="${PERF_SINGLE_WARMUP_SECONDS:-2}"
SINGLE_HWM="${PERF_SINGLE_HWM:-}"
SINGLE_SNDHWM="${PERF_SINGLE_SNDHWM:-}"
SINGLE_RCVHWM="${PERF_SINGLE_RCVHWM:-}"
SINGLE_SNDBUF="${PERF_SINGLE_SNDBUF:-${PERF_SNDBUF:-}}"
SINGLE_RCVBUF="${PERF_SINGLE_RCVBUF:-${PERF_RCVBUF:-}}"
SINGLE_SNDTIMEO_MS="${PERF_SINGLE_SNDTIMEO_MS:-200}"
SINGLE_RCVTIMEO_MS="${PERF_SINGLE_RCVTIMEO_MS:-200}"
PERF_ALLOW_MULTI="${PERF_ALLOW_MULTI:-0}"
if [[ "${PERF_ALLOW_MULTI}" == "1" ]]; then
  PERF_COMPARISON_SCRIPT="${ROOT_DIR}/core/perf/run_comparison.py"
else
  PERF_COMPARISON_SCRIPT="${ROOT_DIR}/core/perf/single/run_comparison.py"
fi

usage() {
  cat <<'USAGE'
Usage: bindings/cpp/perf/run_benchmarks.sh [options]

Measure current zlink C++ binding single-pattern performance.

Options:
  -h, --help                  Show this help.
  --pattern NAME              Pattern list (comma-separated) or ALL.
  --build-dir PATH            Official build directory (must be core/build).
  --reuse-build               Reuse existing build directory as-is (skip configure/build).
  --clean-build               Remove build directory and do a clean build.
  --output PATH               Tee console logs to a file.
  --results-dir PATH          Override result root directory.
  --results-tag NAME          Optional tag in saved result filename.
  --runs N                    Iterations per pattern/transport/size (default: 1).
  --recv MODE                 Receive model: callback (default: callback).
  --duration N                Override single duration seconds (default: 5).
  --warmup N                  Override single warmup seconds (default: 2).
  --hwm N                     Override PERF_SINGLE_HWM (default: 1000 in binary).
  --send-hwm N                Override PERF_SINGLE_SNDHWM (fallback: --hwm).
  --recv-hwm N                Override PERF_SINGLE_RCVHWM (fallback: --hwm).
  --sndbuf SIZE               Override PERF_SINGLE_SNDBUF (e.g. 64b, 1k, 64k).
  --rcvbuf SIZE               Override PERF_SINGLE_RCVBUF (e.g. 64b, 1k, 64k).
  --sndtimeo N                Override PERF_SINGLE_SNDTIMEO_MS (default: 200).
  --rcvtimeo N                Override PERF_SINGLE_RCVTIMEO_MS (default: 200).
  --send-timeout-ms N         Alias of --sndtimeo.
  --recv-timeout-ms N         Alias of --rcvtimeo.
  --pin-cpu                   Pin CPU core during benchmark runs (Linux taskset).
  --io-threads N              Set PERF_IO_THREADS for benchmark binaries.
  --msg-sizes LIST            Comma-separated sizes (e.g., 64,1024,65536).
  --transports LIST           Comma-separated transports.

Notes:
  - result is saved under results/<single|multi>/report/ as
    perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt.
  - default build mode is incremental (configure/build without deleting build dir).
  - --output and result save can be used together.
  - single mode rejects multi patterns; run_benchmarks_multi.sh enables multi mode.
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

normalize_multi_pattern_token() {
  local raw="${1:-}"
  raw="$(printf '%s' "${raw}" | tr '[:lower:]' '[:upper:]')"

  if [[ "${raw}" == MULTI_* ]]; then
    raw="${raw#MULTI_}"
  fi

  case "${raw}" in
    STREAM|STREAMS)
      printf '%s' "STREAM"
      ;;
    *)
      printf '%s' "${raw}"
      ;;
  esac
}

display_pattern_token() {
  local raw="${1:-}"
  if [[ "${PERF_ALLOW_MULTI:-0}" == "1" ]]; then
    raw="$(normalize_multi_pattern_token "${raw}")"
    if [[ -n "${raw}" ]]; then
      printf 'MULTI_%s' "${raw}"
    fi
    return
  fi
  printf '%s' "${raw}"
}

display_pattern_csv() {
  local items=()
  local item=""
  for item in "$@"; do
    items+=( "$(display_pattern_token "${item}")" )
  done
  local IFS=,
  printf '%s' "${items[*]}"
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
    --recv)
      RECV_MODE="${2:-}"
      shift
      ;;
    --duration)
      SINGLE_DURATION_SECONDS="${2:-}"
      shift
      ;;
    --warmup)
      SINGLE_WARMUP_SECONDS="${2:-}"
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
    --sndbuf)
      SINGLE_SNDBUF="${2:-}"
      shift
      ;;
    --rcvbuf)
      SINGLE_RCVBUF="${2:-}"
      shift
      ;;
    --sndtimeo|--send-timeout-ms)
      SINGLE_SNDTIMEO_MS="${2:-}"
      shift
      ;;
    --rcvtimeo|--recv-timeout-ms)
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
  if [[ "${PERF_ALLOW_MULTI}" == "1" ]]; then
    PATTERN="${MULTI_PATTERNS}"
  else
    PATTERN="${STANDARD_PATTERNS}"
  fi
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
  if [[ "${PERF_ALLOW_MULTI}" == "1" ]]; then
    PATTERN_LIST[i]="$(normalize_multi_pattern_token "${PATTERN_LIST[i]}")"
  fi
done

PATTERN_COUNT=0
SINGLE_PATTERN_COUNT=0
for p in "${PATTERN_LIST[@]}"; do
  if [[ "${PERF_ALLOW_MULTI}" == "1" ]]; then
    case ",${MULTI_PATTERNS}," in
      *,"${p}",*)
        PATTERN_COUNT=$((PATTERN_COUNT + 1))
        ;;
      *)
        echo "Error: unsupported multi pattern: ${p}" >&2
        echo "Supported multi patterns: ${MULTI_PATTERNS}" >&2
        exit 1
        ;;
    esac
  else
    case ",${STANDARD_PATTERNS}," in
      *,"${p}",*)
        SINGLE_PATTERN_COUNT=$((SINGLE_PATTERN_COUNT + 1))
        ;;
      *)
        echo "Error: unsupported single pattern: ${p}" >&2
        echo "Supported single patterns: ${STANDARD_PATTERNS}" >&2
        exit 1
        ;;
    esac
  fi
done

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
if [[ -n "${SINGLE_WARMUP_SECONDS}" && ( ! "${SINGLE_WARMUP_SECONDS}" =~ ^[0-9]+$ || "${SINGLE_WARMUP_SECONDS}" -lt 1 ) ]]; then
  echo "warmup must be a positive integer." >&2
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

if [[ "${RUNS_EXPLICIT}" -eq 0 ]]; then
  RUNS=1
fi
if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  exit 1
fi
if [[ "${RECV_MODE}" != "recv" && "${RECV_MODE}" != "callback" ]]; then
  echo "recv mode must be 'recv' or 'callback'." >&2
  exit 1
fi

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
ROOT_DIR="$(realpath -m "${ROOT_DIR}")"
PERF_COMPARISON_SCRIPT="$(realpath -m "${PERF_COMPARISON_SCRIPT}")"
OFFICIAL_BUILD_DIR="${ROOT_DIR}/core/build"

if [[ "${BUILD_DIR}" != "${OFFICIAL_BUILD_DIR}" ]]; then
  echo "Build directory must be exactly: ${OFFICIAL_BUILD_DIR}" >&2
  exit 1
fi

if [[ -z "${RESULTS_DIR}" ]]; then
  RESULTS_DIR="${SCRIPT_DIR}/results"
fi
if [[ -n "${RESULTS_DIR}" ]]; then
  RESULTS_DIR="$(realpath -m "${RESULTS_DIR}")"
fi

TS="$(date +%Y%m%d_%H%M%S)"
NAME="perf_${PLATFORM}_${RECV_MODE}_${TS}"
if [[ -n "${RESULTS_TAG}" ]]; then
  NAME="${NAME}_${RESULTS_TAG}"
fi
RESULT_SUITE="single"
if (( PATTERN_COUNT > 0 )); then
  RESULT_SUITE="multi"
fi
RESULT_FILE="${RESULTS_DIR}/${RESULT_SUITE}/report/${NAME}.txt"

if [[ -n "${OUTPUT_FILE}" ]]; then
  OUTPUT_FILE="$(realpath -m "${OUTPUT_FILE}")"
fi

if [[ -n "${RESULT_FILE}" && -n "${OUTPUT_FILE}" && "${RESULT_FILE}" == "${OUTPUT_FILE}" ]]; then
  echo "Error: --output cannot point to the same file as result output." >&2
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
      -DENABLE_LTO=OFF \
      -DBUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_TESTS=OFF \
      -DZLINK_BUILD_CPP_BINDINGS=ON \
      -DZLINK_CPP_BUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_BENCH_ZMQ=OFF \
      -DZLINK_BUILD_BENCH_ZLINK=ON \
      -DZLINK_BUILD_BENCH_BEAST=OFF \
      -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF \
      -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF \
      -DZLINK_CXX_STANDARD=17
  else
    cmake -S "${CMAKE_SOURCE_DIR}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_MAKE_PROGRAM="${MAKE_BIN}" \
      -DENABLE_LTO=OFF \
      -DBUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_TESTS=OFF \
      -DZLINK_BUILD_CPP_BINDINGS=ON \
      -DZLINK_CPP_BUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_BENCH_ZMQ=OFF \
      -DZLINK_BUILD_BENCH_ZLINK=ON \
      -DZLINK_BUILD_BENCH_BEAST=OFF \
      -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF \
      -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF \
      -DZLINK_CXX_STANDARD=17
  fi

  CPP_SINGLE_TARGETS=(
    cpp_perf_pair
    cpp_perf_pubsub
    cpp_perf_dealer_dealer
    cpp_perf_dealer_router
    cpp_perf_router_router
    cpp_perf_spot
  )
  CPP_MULTI_TARGETS=(
    cpp_comp_src_dealer_dealer_server
    cpp_comp_src_dealer_dealer_client
    cpp_comp_src_dealer_router_server
    cpp_comp_src_dealer_router_client
    cpp_comp_src_router_router_server
    cpp_comp_src_router_router_client
    cpp_comp_src_pubsub_server
    cpp_comp_src_pubsub_client
    cpp_comp_src_spot_server
    cpp_comp_src_spot_client
    cpp_comp_src_stream_server
    perf_stream_client
  )
  if [[ "${IS_WINDOWS}" -eq 1 ]]; then
    if [[ "${PERF_ALLOW_MULTI:-0}" == "1" ]]; then
      cmake --build "${BUILD_DIR}" --config Release --target "${CPP_MULTI_TARGETS[@]}"
    else
      cmake --build "${BUILD_DIR}" --config Release --target "${CPP_SINGLE_TARGETS[@]}"
    fi
  else
    bash "${NORMALIZE_TIMESTAMPS_SH}" "${BUILD_DIR}"
    if [[ "${PERF_ALLOW_MULTI:-0}" == "1" ]]; then
      cmake --build "${BUILD_DIR}" --target "${CPP_MULTI_TARGETS[@]}"
    else
      cmake --build "${BUILD_DIR}" --target "${CPP_SINGLE_TARGETS[@]}"
    fi
  fi
fi

prepare_cpp_runtime_dir() {
  local suite="${1:-single}"
  "${SCRIPT_DIR}/prepare_cpp_runtime.py" --suite "${suite}"
}

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
if [[ "${PERF_ALLOW_MULTI:-0}" == "1" ]]; then
  RUNTIME_BUILD_DIR="$(prepare_cpp_runtime_dir multi)"
else
  RUNTIME_BUILD_DIR="$(prepare_cpp_runtime_dir single)"
fi

RUN_CMD=("${PYTHON_BIN[@]}" "${PERF_COMPARISON_SCRIPT}" "${PATTERN_CSV}" "--build-dir" "${RUNTIME_BUILD_DIR}" "--runs" "${RUNS}")
RUN_CMD+=("--recv" "${RECV_MODE}")
if [[ "${PIN_CPU}" -eq 1 ]]; then
  RUN_CMD+=("--pin-cpu")
fi
if [[ -n "${SINGLE_DURATION_SECONDS}" ]]; then
  if [[ "${PERF_ALLOW_MULTI:-0}" != "1" ]]; then
    RUN_CMD+=("--duration" "${SINGLE_DURATION_SECONDS}")
  fi
fi

if [[ -n "${RESULTS_DIR}" ]]; then
  RUN_CMD+=("--results-dir" "${RESULTS_DIR}")
fi
if [[ -n "${RESULTS_TAG}" ]]; then
  RUN_CMD+=("--results-tag" "${RESULTS_TAG}")
fi
RUN_CMD+=("--result-file" "${RESULT_FILE}")

RUN_ENV=()
RUN_ENV+=(PYTHONUNBUFFERED=1)
RUN_ENV+=(PERF_RECV_MODE="${RECV_MODE}")
if [[ -n "${PERF_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_IO_THREADS="${PERF_IO_THREADS}")
fi
if [[ -n "${PERF_MSG_SIZES}" ]]; then
  RUN_ENV+=(PERF_MSG_SIZES="${PERF_MSG_SIZES}")
fi
if [[ -n "${PERF_TRANSPORTS}" ]]; then
  RUN_ENV+=(PERF_TRANSPORTS="${PERF_TRANSPORTS}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_DURATION_SECONDS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_DURATION_SECONDS="${SINGLE_DURATION_SECONDS}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_WARMUP_SECONDS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_WARMUP_SECONDS="${SINGLE_WARMUP_SECONDS}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_HWM}" ]]; then
  RUN_ENV+=(PERF_SINGLE_HWM="${SINGLE_HWM}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_SNDHWM}" ]]; then
  RUN_ENV+=(PERF_SINGLE_SNDHWM="${SINGLE_SNDHWM}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_RCVHWM}" ]]; then
  RUN_ENV+=(PERF_SINGLE_RCVHWM="${SINGLE_RCVHWM}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_SNDBUF}" ]]; then
  RUN_ENV+=(PERF_SINGLE_SNDBUF="${SINGLE_SNDBUF}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_RCVBUF}" ]]; then
  RUN_ENV+=(PERF_SINGLE_RCVBUF="${SINGLE_RCVBUF}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_SNDTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_SNDTIMEO_MS="${SINGLE_SNDTIMEO_MS}")
fi
if [[ "${PERF_ALLOW_MULTI:-0}" != "1" && -n "${SINGLE_RCVTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_SINGLE_RCVTIMEO_MS="${SINGLE_RCVTIMEO_MS}")
fi
if [[ "${BUILD_MODE}" == "reuse" ]]; then
  RUN_ENV+=(PERF_NO_AUTOBUILD=1)
fi
if [[ -n "${PERF_DISABLE_RESOURCE_METRICS:-}" ]]; then
  RUN_ENV+=(PERF_DISABLE_RESOURCE_METRICS="${PERF_DISABLE_RESOURCE_METRICS}")
fi

value_or_default() {
  local value="${1:-}"
  local fallback="${2:-}"
  if [[ -n "${value}" ]]; then
    printf '%s' "${value}"
  else
    printf '%s' "${fallback}"
  fi
}

print_effective_option() {
  local key="${1:-}"
  local value="${2:-}"
  printf -- "- %s: %s\n" "${key}" "${value}"
}

EFFECTIVE_SEND_HWM="${SINGLE_SNDHWM:-${SINGLE_HWM:-}}"
EFFECTIVE_RECV_HWM="${SINGLE_RCVHWM:-${SINGLE_HWM:-}}"
if [[ -z "${ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM:-}" && -n "${EFFECTIVE_SEND_HWM}" ]]; then
  RUN_ENV+=(ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM="${EFFECTIVE_SEND_HWM}")
fi
DISPLAY_PATTERN_CSV="${PATTERN_CSV}"
DISPLAY_DURATION_SECONDS="${SINGLE_DURATION_SECONDS}"
DISPLAY_WARMUP_SECONDS="${SINGLE_WARMUP_SECONDS}"
DISPLAY_HWM="${SINGLE_HWM}"
DISPLAY_SEND_HWM="${EFFECTIVE_SEND_HWM}"
DISPLAY_RECV_HWM="${EFFECTIVE_RECV_HWM}"
DISPLAY_SNDBUF="${SINGLE_SNDBUF}"
DISPLAY_RCVBUF="${SINGLE_RCVBUF}"
DISPLAY_SNDTIMEO_MS="${SINGLE_SNDTIMEO_MS}"
DISPLAY_RCVTIMEO_MS="${SINGLE_RCVTIMEO_MS}"
if [[ "${PERF_ALLOW_MULTI:-0}" == "1" ]]; then
  DISPLAY_PATTERN_CSV="$(display_pattern_csv "${PATTERN_LIST[@]}")"
  DISPLAY_DURATION_SECONDS="${PERF_MULTI_DURATION_SECONDS:-${SINGLE_DURATION_SECONDS}}"
  DISPLAY_WARMUP_SECONDS="${PERF_MULTI_WARMUP_SECONDS:-${SINGLE_WARMUP_SECONDS}}"
  DISPLAY_HWM="${PERF_MULTI_HWM:-}"
  DISPLAY_SEND_HWM="${PERF_MULTI_SNDHWM:-${DISPLAY_HWM}}"
  DISPLAY_RECV_HWM="${PERF_MULTI_RCVHWM:-${DISPLAY_HWM}}"
  DISPLAY_SNDBUF="${PERF_MULTI_SNDBUF:-}"
  DISPLAY_RCVBUF="${PERF_MULTI_RCVBUF:-}"
  DISPLAY_SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-${SINGLE_SNDTIMEO_MS}}"
  DISPLAY_RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-${SINGLE_RCVTIMEO_MS}}"
fi
DEFAULT_IO_THREADS_LABEL="default(binary=1)"
if [[ "${PERF_ALLOW_MULTI:-0}" == "1" ]]; then
  DEFAULT_IO_THREADS_LABEL="default(policy)"
fi
EFFECTIVE_IO_THREADS="$(value_or_default "${PERF_IO_THREADS}" "${DEFAULT_IO_THREADS_LABEL}")"

echo
echo "## Effective Options (runner)"
print_effective_option "patterns" "${DISPLAY_PATTERN_CSV}"
print_effective_option "build_dir" "${BUILD_DIR}"
print_effective_option "build_mode" "${BUILD_MODE}"
print_effective_option "reuse_build" "$( [[ "${BUILD_MODE}" == "reuse" ]] && echo 1 || echo 0 )"
print_effective_option "clean_build" "$( [[ "${BUILD_MODE}" == "clean" ]] && echo 1 || echo 0 )"
print_effective_option "runs" "${RUNS}"
print_effective_option "recv_mode" "${RECV_MODE}"
print_effective_option "duration_seconds" "${DISPLAY_DURATION_SECONDS}"
print_effective_option "warmup_seconds" "${DISPLAY_WARMUP_SECONDS}"
print_effective_option "hwm" "$(value_or_default "${DISPLAY_HWM}" "default(binary)")"
print_effective_option "send_hwm" "$(value_or_default "${DISPLAY_SEND_HWM}" "default(binary)")"
print_effective_option "recv_hwm" "$(value_or_default "${DISPLAY_RECV_HWM}" "default(binary)")"
print_effective_option "sndbuf" "$(value_or_default "${DISPLAY_SNDBUF}" "default(os)")"
print_effective_option "rcvbuf" "$(value_or_default "${DISPLAY_RCVBUF}" "default(os)")"
print_effective_option "sndtimeo_ms" "${DISPLAY_SNDTIMEO_MS}"
print_effective_option "rcvtimeo_ms" "${DISPLAY_RCVTIMEO_MS}"
print_effective_option "pin_cpu" "${PIN_CPU}"
print_effective_option "io_threads" "${EFFECTIVE_IO_THREADS}"
print_effective_option "msg_sizes" "$(value_or_default "${PERF_MSG_SIZES}" "default(benchmark)")"
print_effective_option "transports" "$(value_or_default "${PERF_TRANSPORTS}" "default(benchmark)")"
print_effective_option "results_dir" "${RESULTS_DIR}"
print_effective_option "results_tag" "$(value_or_default "${RESULTS_TAG}" "none")"
print_effective_option "result_file" "${RESULT_FILE}"
print_effective_option "output_file" "$(value_or_default "${OUTPUT_FILE}" "none")"
print_effective_option "comparison_script" "${PERF_COMPARISON_SCRIPT}"
print_effective_option "runtime_build_dir" "${RUNTIME_BUILD_DIR}"
print_effective_option "python" "${PYTHON_BIN[*]}"
echo
echo "## Effective Env (runner)"
for entry in "${RUN_ENV[@]}"; do
  key="${entry%%=*}"
  value="${entry#*=}"
  print_effective_option "${key}" "${value}"
done
echo

SHOW_TOTAL_TIME=1
if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}" | tee "${OUTPUT_FILE}"
else
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}"
fi
