#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
exec env ZLINK_PERF_BINDING="dotnet" "${ROOT_DIR}/bindings/perf/run_binding_single.sh" "$@"

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
IS_MULTI_MODE=0
if (( MULTI_PATTERN_COUNT > 0 )); then
  IS_MULTI_MODE=1
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

if [[ "${RUNS_EXPLICIT}" -eq 0 ]]; then
  RUNS=1
fi
if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
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

DOTNET_SINGLE_ARTIFACT="${SCRIPT_DIR}/single/Zlink.BindingBench/bin/Release/net8.0/Zlink.BindingBench.dll"
DOTNET_MULTI_ARTIFACT="${SCRIPT_DIR}/multi/Zlink.BindingBench.Multi/bin/Release/net8.0/Zlink.BindingBench.Multi.dll"

dotnet_artifact_for_suite() {
  if (( MULTI_PATTERN_COUNT > 0 )); then
    printf '%s' "${DOTNET_MULTI_ARTIFACT}"
  else
    printf '%s' "${DOTNET_SINGLE_ARTIFACT}"
  fi
}

clean_dotnet_artifacts() {
  rm -rf "${SCRIPT_DIR}/single/Zlink.BindingBench/bin"
  rm -rf "${SCRIPT_DIR}/single/Zlink.BindingBench/obj"
  rm -rf "${SCRIPT_DIR}/multi/Zlink.BindingBench.Multi/bin"
  rm -rf "${SCRIPT_DIR}/multi/Zlink.BindingBench.Multi/obj"
}

case "${BUILD_MODE}" in
  reuse)
    REQUIRED_ARTIFACT="$(dotnet_artifact_for_suite)"
    if [[ ! -f "${REQUIRED_ARTIFACT}" ]]; then
      echo "Error: --reuse-build requires an existing artifact: ${REQUIRED_ARTIFACT}" >&2
      exit 1
    fi
    echo "Reusing benchmark artifact: ${REQUIRED_ARTIFACT}"
    ;;
  clean)
    echo "Cleaning dotnet benchmark artifacts"
    clean_dotnet_artifacts
    rm -rf "${BUILD_DIR}"
    ;;
  incremental)
    echo "Using incremental build mode (auto-build on demand)."
    ;;
  *)
    echo "Error: invalid build mode: ${BUILD_MODE}" >&2
    exit 1
    ;;
esac

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
if [[ "${BUILD_MODE}" == "reuse" ]]; then
  RUN_CMD+=("--reuse-build")
fi
if [[ "${PIN_CPU}" -eq 1 ]]; then
  RUN_CMD+=("--pin-cpu")
fi
if [[ -n "${SINGLE_DURATION_SECONDS}" ]]; then
  RUN_CMD+=("--duration" "${SINGLE_DURATION_SECONDS}")
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
if [[ -n "${PERF_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_IO_THREADS="${PERF_IO_THREADS}")
fi
if [[ -n "${PERF_MSG_SIZES}" ]]; then
  RUN_ENV+=(PERF_MSG_SIZES="${PERF_MSG_SIZES}")
fi
if [[ -n "${PERF_TRANSPORTS}" ]]; then
  RUN_ENV+=(PERF_TRANSPORTS="${PERF_TRANSPORTS}")
fi
EFFECTIVE_MULTI_WARMUP_SECONDS="${MULTI_WARMUP_SECONDS:-3}"
EFFECTIVE_MULTI_DURATION_SECONDS="${MULTI_DURATION_SECONDS:-${SINGLE_DURATION_SECONDS:-5}}"
EFFECTIVE_MULTI_HWM="${MULTI_HWM:-${SINGLE_HWM:-}}"
EFFECTIVE_MULTI_SNDHWM="${MULTI_SNDHWM:-${SINGLE_SNDHWM:-}}"
EFFECTIVE_MULTI_RCVHWM="${MULTI_RCVHWM:-${SINGLE_RCVHWM:-}}"
EFFECTIVE_MULTI_SNDTIMEO_MS="${MULTI_SNDTIMEO_MS:-${SINGLE_SNDTIMEO_MS:-5000}}"
EFFECTIVE_MULTI_RCVTIMEO_MS="${MULTI_RCVTIMEO_MS:-${SINGLE_RCVTIMEO_MS:-5000}}"

if (( IS_MULTI_MODE == 1 )); then
  RUN_ENV+=(PERF_MULTI_WARMUP_SECONDS="${EFFECTIVE_MULTI_WARMUP_SECONDS}")
  RUN_ENV+=(PERF_MULTI_DURATION_SECONDS="${EFFECTIVE_MULTI_DURATION_SECONDS}")
  if [[ -n "${MULTI_CLIENTS}" ]]; then
    RUN_ENV+=(PERF_MULTI_CLIENTS="${MULTI_CLIENTS}")
  fi
  if [[ -n "${EFFECTIVE_MULTI_HWM}" ]]; then
    RUN_ENV+=(PERF_MULTI_HWM="${EFFECTIVE_MULTI_HWM}")
  fi
  if [[ -n "${EFFECTIVE_MULTI_SNDHWM}" ]]; then
    RUN_ENV+=(PERF_MULTI_SNDHWM="${EFFECTIVE_MULTI_SNDHWM}")
  fi
  if [[ -n "${EFFECTIVE_MULTI_RCVHWM}" ]]; then
    RUN_ENV+=(PERF_MULTI_RCVHWM="${EFFECTIVE_MULTI_RCVHWM}")
  fi
  if [[ -n "${EFFECTIVE_MULTI_SNDTIMEO_MS}" ]]; then
    RUN_ENV+=(PERF_MULTI_SNDTIMEO_MS="${EFFECTIVE_MULTI_SNDTIMEO_MS}")
  fi
  if [[ -n "${EFFECTIVE_MULTI_RCVTIMEO_MS}" ]]; then
    RUN_ENV+=(PERF_MULTI_RCVTIMEO_MS="${EFFECTIVE_MULTI_RCVTIMEO_MS}")
  fi
  if [[ -n "${MULTI_SERVER_IO_THREADS}" ]]; then
    RUN_ENV+=(PERF_MULTI_SERVER_IO_THREADS="${MULTI_SERVER_IO_THREADS}")
  fi
  if [[ -n "${MULTI_CLIENT_IO_THREADS}" ]]; then
    RUN_ENV+=(PERF_MULTI_CLIENT_IO_THREADS="${MULTI_CLIENT_IO_THREADS}")
  fi
else
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
EFFECTIVE_IO_THREADS="${PERF_IO_THREADS:-0}"
EFFECTIVE_MULTI_SEND_HWM="${EFFECTIVE_MULTI_SNDHWM:-${EFFECTIVE_MULTI_HWM:-}}"
EFFECTIVE_MULTI_RECV_HWM="${EFFECTIVE_MULTI_RCVHWM:-${EFFECTIVE_MULTI_HWM:-}}"

echo
echo "## Effective Options (runner)"
print_effective_option "pattern" "${PATTERN_CSV}"
print_effective_option "build_dir" "${BUILD_DIR}"
print_effective_option "build_mode" "${BUILD_MODE}"
print_effective_option "reuse_build" "$( [[ "${BUILD_MODE}" == "reuse" ]] && echo 1 || echo 0 )"
print_effective_option "clean_build" "$( [[ "${BUILD_MODE}" == "clean" ]] && echo 1 || echo 0 )"
print_effective_option "runs" "${RUNS}"
print_effective_option "pin_cpu" "${PIN_CPU}"
if (( IS_MULTI_MODE == 1 )); then
  print_effective_option "suite" "multi"
  print_effective_option "warmup_seconds" "${EFFECTIVE_MULTI_WARMUP_SECONDS}"
  print_effective_option "duration_seconds" "${EFFECTIVE_MULTI_DURATION_SECONDS}"
  print_effective_option "clients" "$(value_or_default "${MULTI_CLIENTS}" "default(policy)")"
  print_effective_option "hwm" "$(value_or_default "${EFFECTIVE_MULTI_HWM}" "default(policy)")"
  print_effective_option "send_hwm" "$(value_or_default "${EFFECTIVE_MULTI_SEND_HWM}" "default(policy)")"
  print_effective_option "recv_hwm" "$(value_or_default "${EFFECTIVE_MULTI_RECV_HWM}" "default(policy)")"
  print_effective_option "sndtimeo_ms" "${EFFECTIVE_MULTI_SNDTIMEO_MS}"
  print_effective_option "rcvtimeo_ms" "${EFFECTIVE_MULTI_RCVTIMEO_MS}"
  print_effective_option "server_io_threads" "$(value_or_default "${MULTI_SERVER_IO_THREADS}" "default(policy)")"
  print_effective_option "client_io_threads" "$(value_or_default "${MULTI_CLIENT_IO_THREADS}" "default(policy)")"
else
  print_effective_option "suite" "single"
  print_effective_option "duration_seconds" "${SINGLE_DURATION_SECONDS}"
  print_effective_option "hwm" "$(value_or_default "${SINGLE_HWM}" "default(binary)")"
  print_effective_option "send_hwm" "$(value_or_default "${EFFECTIVE_SEND_HWM}" "default(binary)")"
  print_effective_option "recv_hwm" "$(value_or_default "${EFFECTIVE_RECV_HWM}" "default(binary)")"
  print_effective_option "sndtimeo_ms" "${SINGLE_SNDTIMEO_MS}"
  print_effective_option "rcvtimeo_ms" "${SINGLE_RCVTIMEO_MS}"
  print_effective_option "io_threads" "${EFFECTIVE_IO_THREADS}"
fi
print_effective_option "msg_sizes" "$(value_or_default "${PERF_MSG_SIZES}" "default(benchmark)")"
print_effective_option "transports" "$(value_or_default "${PERF_TRANSPORTS}" "default(benchmark)")"
print_effective_option "results_dir" "${RESULTS_DIR}"
print_effective_option "results_tag" "$(value_or_default "${RESULTS_TAG}" "none")"
print_effective_option "result_file" "${RESULT_FILE}"
print_effective_option "output_file" "$(value_or_default "${OUTPUT_FILE}" "none")"
print_effective_option "comparison_script" "${PERF_COMPARISON_SCRIPT}"
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
