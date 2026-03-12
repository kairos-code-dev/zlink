#!/usr/bin/env bash
set -euo pipefail
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BINDING="${ZLINK_PERF_BINDING:-}"
if [[ -z "${BINDING}" ]]; then
  echo "Error: ZLINK_PERF_BINDING is required." >&2
  exit 1
fi

binding_default_build_dir() {
  case "${BINDING}" in
    cpp)
      printf '%s' "${ROOT_DIR}/bindings/cpp/perf/single/build"
      ;;
    dotnet)
      printf '%s' "${ROOT_DIR}/bindings/dotnet/perf/single/Zlink.BindingBench/bin/Release/net8.0"
      ;;
    java)
      printf '%s' "${ROOT_DIR}/bindings/java/perf/single/Zlink.PerfBench/build"
      ;;
    *)
      echo "Error: unsupported binding: ${BINDING}" >&2
      exit 1
      ;;
  esac
}

clean_binding_builds() {
  case "${BINDING}" in
    cpp)
      rm -rf "${ROOT_DIR}/bindings/cpp/perf/single/build"
      ;;
    dotnet)
      rm -rf \
        "${ROOT_DIR}/bindings/dotnet/perf/single/Zlink.BindingBench/bin" \
        "${ROOT_DIR}/bindings/dotnet/perf/single/Zlink.BindingBench/obj"
      ;;
    java)
      rm -rf "${ROOT_DIR}/bindings/java/perf/single/Zlink.PerfBench/build"
      ;;
  esac
}

python_bin() {
  if command -v python3 >/dev/null 2>&1; then
    printf '%s' "python3"
  elif command -v python >/dev/null 2>&1; then
    printf '%s' "python"
  else
    echo "Python not found. Install Python 3 or ensure it is on PATH." >&2
    exit 1
  fi
}

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

PLATFORM="linux"
case "$(uname -s)" in
  Darwin*)
    PLATFORM="macos"
    ;;
  Linux*)
    PLATFORM="linux"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    PLATFORM="windows"
    ;;
esac

PATTERN="ALL"
OUTPUT_FILE=""
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
SINGLE_SNDBUF="${PERF_SINGLE_SNDBUF:-${PERF_SNDBUF:-}}"
SINGLE_RCVBUF="${PERF_SINGLE_RCVBUF:-${PERF_RCVBUF:-}}"
SINGLE_SNDTIMEO_MS="${PERF_SINGLE_SNDTIMEO_MS:-200}"
SINGLE_RCVTIMEO_MS="${PERF_SINGLE_RCVTIMEO_MS:-200}"
BUILD_DIR="$(binding_default_build_dir)"
RUNNER_SCRIPT="${ROOT_DIR}/bindings/perf/run_policy_bench.py"

usage() {
  cat <<USAGE
Usage: bindings/${BINDING}/perf/run_benchmarks.sh [options]

Measure current ${BINDING} single-pattern performance.

Options:
  -h, --help                  Show this help.
  --pattern NAME              Pattern list (comma-separated) or ALL.
  --build-dir PATH            Build directory (binding-specific default).
  --reuse-build               Reuse existing build directory as-is (skip rebuild).
  --clean-build               Remove build artifacts and do a clean build.
  --output PATH               Tee console logs to a file.
  --results-dir PATH          Override result root directory.
  --results-tag NAME          Optional tag in saved result filename.
  --runs N                    Iterations per pattern/transport/size (default: 1).
  --duration N                Override single duration seconds (default: 5).
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
  - result is saved under results/single/report/.
  - default build mode is incremental (rebuild only what is needed).
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

IFS=',' read -r -a PATTERN_LIST <<< "${PATTERN}"
PATTERN_LIST=("${PATTERN_LIST[@]}")
PATTERN_COUNT=0
for i in "${!PATTERN_LIST[@]}"; do
  PATTERN_LIST[$i]="$(printf '%s' "${PATTERN_LIST[$i]}" | tr '[:lower:]' '[:upper:]')"
  if [[ "${PATTERN_LIST[$i]}" == MULTI_* ]]; then
    echo "Error: multi patterns are not allowed here: ${PATTERN_LIST[$i]}" >&2
    exit 1
  fi
  if [[ -n "${PATTERN_LIST[$i]}" && "${PATTERN_LIST[$i]}" != "ALL" ]]; then
    PATTERN_COUNT=$((PATTERN_COUNT + 1))
  fi
done

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

if [[ "${RUNS_EXPLICIT}" -eq 0 ]]; then
  RUNS=1
fi
if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  exit 1
fi

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
ROOT_DIR="$(realpath -m "${ROOT_DIR}")"
RUNNER_SCRIPT="$(realpath -m "${RUNNER_SCRIPT}")"

if [[ "${BUILD_DIR}" != "${ROOT_DIR}/"* ]]; then
  echo "Build directory must be inside repo root: ${ROOT_DIR}" >&2
  exit 1
fi

if [[ -z "${RESULTS_DIR}" ]]; then
  RESULTS_DIR="${ROOT_DIR}/bindings/${BINDING}/perf/results"
fi
RESULTS_DIR="$(realpath -m "${RESULTS_DIR}")"

TS="$(date +%Y%m%d_%H%M%S)"
NAME="perf_${PLATFORM}_${TS}"
if [[ -n "${RESULTS_TAG}" ]]; then
  NAME="${NAME}_${RESULTS_TAG}"
fi
RESULT_FILE="${RESULTS_DIR}/single/report/${NAME}.txt"

if [[ -n "${OUTPUT_FILE}" ]]; then
  OUTPUT_FILE="$(realpath -m "${OUTPUT_FILE}")"
fi

if [[ -n "${RESULT_FILE}" && -n "${OUTPUT_FILE}" && "${RESULT_FILE}" == "${OUTPUT_FILE}" ]]; then
  echo "Error: --output cannot point to the same file as result output." >&2
  exit 1
fi

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
    clean_binding_builds
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
echo "Using CMake source directory: ${CMAKE_SOURCE_DIR}"

PYTHON_BIN="$(python_bin)"
PATTERN_CSV="$(IFS=,; echo "${PATTERN_LIST[*]}")"

RUN_CMD=("${PYTHON_BIN}" "${RUNNER_SCRIPT}" "--binding" "${BINDING}" "--suite" "single" "--pattern" "${PATTERN_CSV}" "--build-dir" "${BUILD_DIR}" "--runs" "${RUNS}" "--results-dir" "${RESULTS_DIR}" "--results-tag" "${RESULTS_TAG}" "--result-file" "${RESULT_FILE}")
if [[ "${PIN_CPU}" -eq 1 ]]; then
  RUN_CMD+=("--pin-cpu")
fi
if [[ "${BUILD_MODE}" == "reuse" ]]; then
  RUN_CMD+=("--reuse-build")
fi

RUN_ENV=()
RUN_ENV+=(PYTHONUNBUFFERED=1)
RUN_ENV+=(PERF_OUTPUT_STYLE="core")
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
if [[ -n "${SINGLE_SNDBUF}" ]]; then
  RUN_ENV+=(PERF_SINGLE_SNDBUF="${SINGLE_SNDBUF}")
fi
if [[ -n "${SINGLE_RCVBUF}" ]]; then
  RUN_ENV+=(PERF_SINGLE_RCVBUF="${SINGLE_RCVBUF}")
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
EFFECTIVE_IO_THREADS="$(value_or_default "${PERF_IO_THREADS}" "default(binary=2)")"

echo
echo "## Effective Options (runner)"
print_effective_option "pattern" "${PATTERN_CSV}"
print_effective_option "build_dir" "${BUILD_DIR}"
print_effective_option "build_mode" "${BUILD_MODE}"
print_effective_option "reuse_build" "$( [[ "${BUILD_MODE}" == "reuse" ]] && echo 1 || echo 0 )"
print_effective_option "clean_build" "$( [[ "${BUILD_MODE}" == "clean" ]] && echo 1 || echo 0 )"
print_effective_option "runs" "${RUNS}"
print_effective_option "duration_seconds" "${SINGLE_DURATION_SECONDS}"
print_effective_option "hwm" "$(value_or_default "${SINGLE_HWM}" "default(binary)")"
print_effective_option "send_hwm" "$(value_or_default "${EFFECTIVE_SEND_HWM}" "default(binary)")"
print_effective_option "recv_hwm" "$(value_or_default "${EFFECTIVE_RECV_HWM}" "default(binary)")"
print_effective_option "sndbuf" "$(value_or_default "${SINGLE_SNDBUF}" "default(os)")"
print_effective_option "rcvbuf" "$(value_or_default "${SINGLE_RCVBUF}" "default(os)")"
print_effective_option "sndtimeo_ms" "${SINGLE_SNDTIMEO_MS}"
print_effective_option "rcvtimeo_ms" "${SINGLE_RCVTIMEO_MS}"
print_effective_option "pin_cpu" "${PIN_CPU}"
print_effective_option "io_threads" "${EFFECTIVE_IO_THREADS}"
print_effective_option "msg_sizes" "$(value_or_default "${PERF_MSG_SIZES}" "default(benchmark)")"
print_effective_option "transports" "$(value_or_default "${PERF_TRANSPORTS}" "default(benchmark)")"
print_effective_option "results_dir" "${RESULTS_DIR}"
print_effective_option "results_tag" "$(value_or_default "${RESULTS_TAG}" "none")"
print_effective_option "result_file" "${RESULT_FILE}"
print_effective_option "output_file" "$(value_or_default "${OUTPUT_FILE}" "none")"
print_effective_option "comparison_script" "${RUNNER_SCRIPT}"
print_effective_option "python" "${PYTHON_BIN}"
echo
echo "## Effective Env (runner)"
for entry in "${RUN_ENV[@]}"; do
  key="${entry%%=*}"
  value="${entry#*=}"
  if [[ "${key}" == "PERF_OUTPUT_STYLE" ]]; then
    continue
  fi
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
