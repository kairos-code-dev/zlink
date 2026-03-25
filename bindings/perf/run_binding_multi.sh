#!/usr/bin/env bash
set -euo pipefail

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
      printf '%s' "${ROOT_DIR}/bindings/cpp/perf/multi/build"
      ;;
    dotnet)
      printf '%s' "${ROOT_DIR}/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/bin/Release/net8.0"
      ;;
    java)
      printf '%s' "${ROOT_DIR}/bindings/java/perf/multi/Zlink.PerfBench/build"
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
      rm -rf "${ROOT_DIR}/bindings/cpp/perf/multi/build"
      ;;
    dotnet)
      rm -rf \
        "${ROOT_DIR}/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/bin" \
        "${ROOT_DIR}/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/obj"
      ;;
    java)
      rm -rf "${ROOT_DIR}/bindings/java/perf/multi/Zlink.PerfBench/build"
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

PATTERNS="DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,SPOT,STREAM,STREAM_CALLBACK,STREAM_LEN32BE"
TRANSPORTS_DEFAULT="tcp,tls,ws,wss"
IFS=',' read -r -a PATTERN_LIST <<< "${PATTERNS}"
BUILD_DIR="$(binding_default_build_dir)"
RUNNER_SCRIPT="${ROOT_DIR}/bindings/perf/run_policy_bench.py"

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

is_uint() {
  local value="${1:-}"
  [[ "${value}" =~ ^[0-9]+$ ]]
}

first_env_value() {
  local name=""
  for name in "$@"; do
    if [[ -n "${!name:-}" ]]; then
      printf '%s' "${!name}"
      return 0
    fi
  done
  return 1
}

env_or_default() {
  local default_value="${1:-}"
  shift || true
  local resolved=""
  if resolved="$(first_env_value "$@" 2>/dev/null)"; then
    printf '%s' "${resolved}"
  else
    printf '%s' "${default_value}"
  fi
}

NOFILE_SKIP_REASON=""
ensure_nofile_limit() {
  local clients="${1:-}"
  NOFILE_SKIP_REASON=""

  if [[ "${PERF_SKIP_NOFILE_CHECK:-0}" == "1" ]]; then
    return 0
  fi
  if ! is_uint "${clients}"; then
    return 0
  fi

  local required=$(( clients * 3 + 4096 ))
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

  NOFILE_SKIP_REASON="clients=${clients},required=${required},soft=${soft},hard=${hard}"
  return 1
}

MEMORY_SKIP_REASON=""
memory_available_kb() {
  if [[ "${PERF_SKIP_MEMORY_CHECK:-0}" == "1" ]]; then
    echo ""
    return
  fi
  if [[ ! -r /proc/meminfo ]]; then
    echo ""
    return
  fi
  awk '/^MemAvailable:/ { print $2; found=1; exit } END { if (!found) print "" }' /proc/meminfo 2>/dev/null || true
}

resolve_memory_max_clients() {
  local available_kb
  available_kb="$(memory_available_kb)"
  if ! is_uint "${available_kb}"; then
    echo ""
    return
  fi

  local budget_pct
  local base_mb
  local per_client_kb
  budget_pct="$(env_or_default "70" PERF_MEMORY_BUDGET_PCT PERF_MULTI_MEMORY_BUDGET_PCT)"
  base_mb="$(env_or_default "512" PERF_MEMORY_BASE_MB PERF_MULTI_MEMORY_BASE_MB)"
  per_client_kb="$(env_or_default "1024" PERF_MEMORY_PER_CLIENT_KB PERF_MULTI_MEMORY_PER_CLIENT_KB)"
  if ! is_uint "${budget_pct}" || (( budget_pct < 1 || budget_pct > 95 )); then
    echo ""
    return
  fi
  if ! is_uint "${base_mb}"; then
    echo ""
    return
  fi
  if ! is_uint "${per_client_kb}" || (( per_client_kb < 1 )); then
    echo ""
    return
  fi

  local usable_kb=$(( available_kb * budget_pct / 100 ))
  local base_kb=$(( base_mb * 1024 ))
  if (( usable_kb <= base_kb )); then
    echo "1"
    return
  fi

  local max_clients=$(( (usable_kb - base_kb) / per_client_kb ))
  if (( max_clients < 1 )); then
    max_clients=1
  fi
  echo "${max_clients}"
}

ensure_memory_budget() {
  local clients="${1:-}"
  MEMORY_SKIP_REASON=""

  if [[ "${PERF_SKIP_MEMORY_CHECK:-0}" == "1" ]]; then
    return 0
  fi
  if ! is_uint "${clients}"; then
    return 0
  fi

  local max_clients
  max_clients="$(resolve_memory_max_clients)"
  if ! is_uint "${max_clients}"; then
    return 0
  fi
  if (( clients <= max_clients )); then
    return 0
  fi

  local available_kb
  available_kb="$(memory_available_kb)"
  local budget_pct
  local base_mb
  local per_client_kb
  budget_pct="$(env_or_default "70" PERF_MEMORY_BUDGET_PCT PERF_MULTI_MEMORY_BUDGET_PCT)"
  base_mb="$(env_or_default "512" PERF_MEMORY_BASE_MB PERF_MULTI_MEMORY_BASE_MB)"
  per_client_kb="$(env_or_default "1024" PERF_MEMORY_PER_CLIENT_KB PERF_MULTI_MEMORY_PER_CLIENT_KB)"
  MEMORY_SKIP_REASON="clients=${clients},max_clients=${max_clients},mem_available_kb=${available_kb},budget_pct=${budget_pct},base_mb=${base_mb},per_client_kb=${per_client_kb}"
  return 1
}

usage() {
  cat <<USAGE
Usage: bindings/${BINDING}/perf/run_benchmarks_multi.sh [options]

Run only multi-socket benchmark patterns.
Default PATTERN is:
  DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,SPOT,STREAM,STREAM_CALLBACK,STREAM_LEN32BE
Legacy MULTI_ prefix is accepted and stripped automatically.
By default, this wrapper runs current ${BINDING} only.
By default, multi-bench keeps warmup at 2s and duration window at 5s.
By default, multi-bench uses transports: tcp,tls,ws,wss (can be overridden with --transports).

Options:
  --pattern NAME         Benchmark pattern (default: all patterns above). Legacy MULTI_ prefix is optional.
                         Alias: stream/streams => STREAM,STREAM_CALLBACK,STREAM_LEN32BE
  --help                 Show this help.
  --reuse-build          Reuse existing build directory as-is (skip rebuild).
  --clean-build          Remove build artifacts and do a clean build.
  --results-dir PATH     Override results root directory.
  --results-tag NAME     Optional tag appended to the results filename.
  --build-dir PATH       Override build directory.
  --output PATH          Tee results to a file.
  --runs N               Iterations per configuration (default: 1).
  --pin-cpu              Pin CPU core during benchmarks (Linux taskset).
  --io-threads N         Legacy alias that sets both server/client io-threads.
  --server-io-threads N  Set PERF_SERVER_IO_THREADS (default: non-stream=2, stream=4).
  --client-io-threads N  Set PERF_CLIENT_IO_THREADS (default: non-stream=2, stream=4).
  --msg-sizes LIST       Comma-separated message sizes.
  --transports LIST      Comma-separated transports.
  --warmup N             Optional override for multi warmup seconds (default 2).
  --duration N           Optional override for multi duration seconds (default 5).
  --clients N            Override number of client sockets per pattern (default: 100, stream=10000).
  --hwm N                Override PERF_HWM (default: 100, stream=10 in binary).
  --send-hwm N           Override PERF_SNDHWM (fallback: --hwm).
  --recv-hwm N           Override PERF_RCVHWM (fallback: --hwm).
  --sndbuf SIZE          Override PERF_SNDBUF (e.g. 64b, 1k, 64k).
  --rcvbuf SIZE          Override PERF_RCVBUF (e.g. 64b, 1k, 64k).
  --sndtimeo N           Override PERF_SNDTIMEO_MS (default: 200).
  --rcvtimeo N           Override PERF_RCVTIMEO_MS (default: 200).
  --send-timeout-ms N    Alias of --sndtimeo.
  --recv-timeout-ms N    Alias of --rcvtimeo.
  --connect-concurrency N
                         Override concurrent connect count.
  --transport-transition-ms N
                         Override PERF_TRANSPORT_TRANSITION_MS (default: 3000).
  --pattern-transition-ms N
                         Override PERF_PATTERN_TRANSITION_MS (default: 3000).
  --server-ready-timeout-ms N
                         Override PERF_SERVER_READY_TIMEOUT_MS (default: 10000).
  --server-shutdown-timeout-ms N
                         Override PERF_SERVER_SHUTDOWN_TIMEOUT_MS (default: 5000).
  --server-bind-port N   Override PERF_SERVER_BIND_PORT (default: 0=auto).
  --connect-ready-timeout-ms N
                         Override PERF_CONNECT_READY_TIMEOUT_MS (default: 5000).
  --monitor-hwm N        Override PERF_MONITOR_HWM (default: 1000).
USAGE
}

resolve_pattern_connect_concurrency() {
  local clients="${1:-}"
  if [[ -n "${CONNECT_CONCURRENCY}" ]]; then
    echo "${CONNECT_CONCURRENCY}"
    return
  fi
  if [[ "${clients}" =~ ^[0-9]+$ ]] && (( clients >= 10000 )); then
    echo "1024"
  else
    echo "128"
  fi
}

add_explicit_pattern_unique() {
  local pattern="${1:-}"
  if [[ -z "${pattern}" ]]; then
    return
  fi
  local existing=""
  for existing in "${EXPLICIT_PATTERNS[@]}"; do
    if [[ "${existing}" == "${pattern}" ]]; then
      return
    fi
  done
  EXPLICIT_PATTERNS+=("${pattern}")
}

expand_and_add_explicit_pattern() {
  local raw="${1:-}"
  raw="${raw#"${raw%%[![:space:]]*}"}"
  raw="${raw%"${raw##*[![:space:]]}"}"
  raw="$(printf '%s' "${raw}" | tr '[:lower:]' '[:upper:]')"
  if [[ -z "${raw}" ]]; then
    return
  fi

  local base="${raw}"
  if [[ "${base}" == MULTI_* ]]; then
    base="${base#MULTI_}"
  fi

  case "${base}" in
    STREAM|STREAMS)
      add_explicit_pattern_unique "STREAM"
      add_explicit_pattern_unique "STREAM_CALLBACK"
      add_explicit_pattern_unique "STREAM_LEN32BE"
      ;;
    *)
      add_explicit_pattern_unique "${base}"
      ;;
  esac
}

HAS_EXPLICIT_TRANSPORT=0
HAS_EXPLICIT_MSG_SIZES=0
HAS_EXPLICIT_RUNS=0
HAS_EXPLICIT_RESULTS_DIR=0
BUILD_MODE="incremental"
BUILD_MODE_EXPLICIT=0
WARMUP_SECONDS="$(env_or_default "2" PERF_WARMUP_SECONDS PERF_MULTI_WARMUP_SECONDS)"
DURATION_SECONDS="$(env_or_default "5" PERF_DURATION_SECONDS PERF_MULTI_DURATION_SECONDS)"
CLIENTS="$(env_or_default "" PERF_CLIENTS PERF_MULTI_CLIENTS)"
HWM="$(env_or_default "" PERF_HWM PERF_MULTI_HWM)"
SNDHWM="$(env_or_default "" PERF_SNDHWM PERF_MULTI_SNDHWM)"
RCVHWM="$(env_or_default "" PERF_RCVHWM PERF_MULTI_RCVHWM)"
SNDBUF="$(env_or_default "" PERF_SNDBUF PERF_MULTI_SNDBUF)"
RCVBUF="$(env_or_default "" PERF_RCVBUF PERF_MULTI_RCVBUF)"
SNDTIMEO_MS="$(env_or_default "200" PERF_SNDTIMEO_MS PERF_MULTI_SNDTIMEO_MS)"
RCVTIMEO_MS="$(env_or_default "200" PERF_RCVTIMEO_MS PERF_MULTI_RCVTIMEO_MS)"
CONNECT_CONCURRENCY="$(env_or_default "" PERF_CONNECT_CONCURRENCY PERF_MULTI_CONNECT_CONCURRENCY)"
TRANSPORT_TRANSITION_MS="$(env_or_default "3000" PERF_TRANSPORT_TRANSITION_MS PERF_MULTI_TRANSPORT_TRANSITION_MS)"
PATTERN_TRANSITION_MS="$(env_or_default "3000" PERF_PATTERN_TRANSITION_MS PERF_MULTI_PATTERN_TRANSITION_MS)"
SERVER_READY_TIMEOUT_MS="$(env_or_default "10000" PERF_SERVER_READY_TIMEOUT_MS PERF_MULTI_SERVER_READY_TIMEOUT_MS)"
SERVER_SHUTDOWN_TIMEOUT_MS="$(env_or_default "5000" PERF_SERVER_SHUTDOWN_TIMEOUT_MS PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS)"
SERVER_BIND_PORT="$(env_or_default "0" PERF_SERVER_BIND_PORT PERF_MULTI_SERVER_BIND_PORT)"
CONNECT_READY_TIMEOUT_MS="$(env_or_default "5000" PERF_CONNECT_READY_TIMEOUT_MS PERF_MULTI_CONNECT_READY_TIMEOUT_MS)"
MONITOR_HWM="$(env_or_default "1000" PERF_MONITOR_HWM PERF_MULTI_MONITOR_HWM)"
SERVER_IO_THREADS="$(env_or_default "" PERF_SERVER_IO_THREADS PERF_MULTI_SERVER_IO_THREADS)"
CLIENT_IO_THREADS="$(env_or_default "" PERF_CLIENT_IO_THREADS PERF_MULTI_CLIENT_IO_THREADS)"
RESULTS_DIR_OVERRIDE="${PERF_RESULTS_DIR:-}"
EXPLICIT_PATTERNS=()
SCRIPT_ARGS=()
OUTPUT_FILE=""
RESULTS_TAG=""
RUNS="1"
PIN_CPU=0
EXPLICIT_TRANSPORTS=""
EXPLICIT_MSG_SIZES=""
DEFAULT_CLIENTS="${PERF_DEFAULT_CLIENTS:-100}"
DEFAULT_STREAM_CLIENTS="${PERF_DEFAULT_STREAM_CLIENTS:-10000}"
DEFAULT_HWM="${PERF_DEFAULT_HWM:-100}"
DEFAULT_STREAM_HWM="${PERF_DEFAULT_STREAM_HWM:-10}"
DEFAULT_IO_THREADS="$(env_or_default "2" PERF_DEFAULT_IO_THREADS PERF_MULTI_DEFAULT_IO_THREADS)"
DEFAULT_STREAM_IO_THREADS="$(env_or_default "4" PERF_DEFAULT_STREAM_IO_THREADS PERF_MULTI_STREAM_DEFAULT_IO_THREADS)"
STREAM_SERVER_IO_THREADS="$(env_or_default "" PERF_STREAM_SERVER_IO_THREADS PERF_MULTI_STREAM_SERVER_IO_THREADS)"
STREAM_CLIENT_IO_THREADS="$(env_or_default "" PERF_STREAM_CLIENT_IO_THREADS PERF_MULTI_STREAM_CLIENT_IO_THREADS)"

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
  arg="$1"
  case "${arg}" in
    -h|--help)
      usage
      exit 0
      ;;
    --transports|--msg-sizes|--results-tag)
      if [[ $# -lt 2 ]]; then
        echo "Error: ${arg} requires a value." >&2
        exit 1
      fi
      if [[ "${arg}" == "--transports" ]]; then
        HAS_EXPLICIT_TRANSPORT=1
        EXPLICIT_TRANSPORTS="${2}"
      elif [[ "${arg}" == "--msg-sizes" ]]; then
        HAS_EXPLICIT_MSG_SIZES=1
        EXPLICIT_MSG_SIZES="${2}"
      else
        RESULTS_TAG="${2}"
      fi
      SCRIPT_ARGS+=("$1" "$2")
      shift 2
      ;;
    --pattern)
      if [[ $# -lt 2 ]]; then
        echo "Error: --pattern requires a value." >&2
        exit 1
      fi
      if [[ "$(printf '%s' "$2" | tr '[:lower:]' '[:upper:]')" == "ALL" ]]; then
        EXPLICIT_PATTERNS=("${PATTERN_LIST[@]}")
        shift 2
        continue
      fi
      IFS=',' read -r -a pattern_list <<< "$2"
      for p in "${pattern_list[@]}"; do
        expand_and_add_explicit_pattern "${p}"
      done
      shift 2
      ;;
    --reuse-build)
      set_build_mode "reuse"
      shift
      ;;
    --clean-build)
      set_build_mode "clean"
      shift
      ;;
    --runs)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      HAS_EXPLICIT_RUNS=1
      RUNS="${2}"
      shift 2
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      BUILD_DIR="${2}"
      shift 2
      ;;
    --output)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      OUTPUT_FILE="${2}"
      shift 2
      ;;
    --warmup)
      WARMUP_SECONDS="${2:-}"
      shift 2
      ;;
    --duration)
      DURATION_SECONDS="${2:-}"
      shift 2
      ;;
    --pin-cpu)
      PIN_CPU=1
      SCRIPT_ARGS+=("$1")
      shift
      ;;
    --results-dir)
      HAS_EXPLICIT_RESULTS_DIR=1
      RESULTS_DIR_OVERRIDE="${2:-}"
      shift 2
      ;;
    --io-threads)
      SERVER_IO_THREADS="${2:-}"
      CLIENT_IO_THREADS="${2:-}"
      shift 2
      ;;
    --server-io-threads)
      SERVER_IO_THREADS="${2:-}"
      shift 2
      ;;
    --client-io-threads)
      CLIENT_IO_THREADS="${2:-}"
      shift 2
      ;;
    --clients)
      CLIENTS="${2:-}"
      shift 2
      ;;
    --hwm)
      HWM="${2:-}"
      shift 2
      ;;
    --send-hwm)
      SNDHWM="${2:-}"
      shift 2
      ;;
    --recv-hwm)
      RCVHWM="${2:-}"
      shift 2
      ;;
    --sndbuf)
      SNDBUF="${2:-}"
      shift 2
      ;;
    --rcvbuf)
      RCVBUF="${2:-}"
      shift 2
      ;;
    --sndtimeo|--send-timeout-ms)
      SNDTIMEO_MS="${2:-}"
      shift 2
      ;;
    --rcvtimeo|--recv-timeout-ms)
      RCVTIMEO_MS="${2:-}"
      shift 2
      ;;
    --connect-concurrency)
      CONNECT_CONCURRENCY="${2:-}"
      shift 2
      ;;
    --transport-transition-ms)
      TRANSPORT_TRANSITION_MS="${2:-}"
      shift 2
      ;;
    --pattern-transition-ms)
      PATTERN_TRANSITION_MS="${2:-}"
      shift 2
      ;;
    --server-ready-timeout-ms)
      SERVER_READY_TIMEOUT_MS="${2:-}"
      shift 2
      ;;
    --server-shutdown-timeout-ms)
      SERVER_SHUTDOWN_TIMEOUT_MS="${2:-}"
      shift 2
      ;;
    --server-bind-port)
      SERVER_BIND_PORT="${2:-}"
      shift 2
      ;;
    --connect-ready-timeout-ms)
      CONNECT_READY_TIMEOUT_MS="${2:-}"
      shift 2
      ;;
    --monitor-hwm)
      MONITOR_HWM="${2:-}"
      shift 2
      ;;
    --*)
      echo "Error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      echo "Error: unknown positional argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! is_uint "${RUNS}" || (( RUNS < 1 )); then
  echo "Runs must be a positive integer." >&2
  exit 1
fi
for name in WARMUP_SECONDS DURATION_SECONDS TRANSPORT_TRANSITION_MS PATTERN_TRANSITION_MS SERVER_READY_TIMEOUT_MS SERVER_SHUTDOWN_TIMEOUT_MS CONNECT_READY_TIMEOUT_MS MONITOR_HWM; do
  value="${!name}"
  if ! is_uint "${value}"; then
    echo "Error: ${name} must be a non-negative integer." >&2
    exit 1
  fi
done
for name in CLIENTS HWM SNDHWM RCVHWM SNDTIMEO_MS RCVTIMEO_MS SERVER_IO_THREADS CLIENT_IO_THREADS CONNECT_CONCURRENCY; do
  value="${!name}"
  if [[ -n "${value}" ]] && ! is_uint "${value}"; then
    echo "Error: ${name} must be a positive integer." >&2
    exit 1
  fi
done
if ! is_uint "${SERVER_BIND_PORT}" || (( SERVER_BIND_PORT > 65535 )); then
  echo "Error: --server-bind-port must be an integer in range 0..65535." >&2
  exit 1
fi

if [[ -z "${CLIENTS}" && -z "${PERF_CLIENTS:-}" ]]; then
  memory_max_clients="$(resolve_memory_max_clients)"
  if is_uint "${memory_max_clients}"; then
    if is_uint "${DEFAULT_CLIENTS}" && (( DEFAULT_CLIENTS > memory_max_clients )); then
      DEFAULT_CLIENTS="${memory_max_clients}"
    fi
    if is_uint "${DEFAULT_STREAM_CLIENTS}" && (( DEFAULT_STREAM_CLIENTS > memory_max_clients )); then
      DEFAULT_STREAM_CLIENTS="${memory_max_clients}"
    fi
  fi
fi

PATTERNS_TO_RUN=("${PATTERN_LIST[@]}")
if [[ "${#EXPLICIT_PATTERNS[@]}" -gt 0 ]]; then
  PATTERNS_TO_RUN=("${EXPLICIT_PATTERNS[@]}")
fi

if [[ "${BUILD_MODE}" == "clean" ]]; then
  clean_binding_builds
  rm -rf "${BUILD_DIR}"
fi
if [[ "${BUILD_MODE}" == "reuse" && ! -d "${BUILD_DIR}" ]]; then
  echo "Error: --reuse-build requires an existing build directory: ${BUILD_DIR}" >&2
  exit 1
fi

if [[ -z "${RESULTS_DIR_OVERRIDE}" ]]; then
  RESULTS_DIR_OVERRIDE="${ROOT_DIR}/bindings/${BINDING}/perf/results"
fi
RESULTS_DIR_OVERRIDE="$(realpath -m "${RESULTS_DIR_OVERRIDE}")"
BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
RUNNER_SCRIPT="$(realpath -m "${RUNNER_SCRIPT}")"

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

TS="$(date +%Y%m%d_%H%M%S)"
NAME="perf_${PLATFORM}_${TS}"
if [[ -n "${RESULTS_TAG}" ]]; then
  NAME="${NAME}_${RESULTS_TAG}"
fi
RESULT_FILE="${RESULTS_DIR_OVERRIDE}/multi/report/${NAME}.txt"

if [[ -n "${OUTPUT_FILE}" ]]; then
  OUTPUT_FILE="$(realpath -m "${OUTPUT_FILE}")"
fi
if [[ -n "${OUTPUT_FILE}" && "${OUTPUT_FILE}" == "${RESULT_FILE}" ]]; then
  echo "Error: --output cannot point to the same file as result output." >&2
  exit 1
fi

RUN_ENV=()
RUN_ENV+=(PYTHONUNBUFFERED=1)
RUN_ENV+=(PERF_OUTPUT_STYLE="core")
RUN_ENV+=(PERF_ALLOW_MULTI="1")
RUN_ENV+=(PERF_RESULTS_DIR="${RESULTS_DIR_OVERRIDE}")
RUN_ENV+=(PERF_WARMUP_SECONDS="${WARMUP_SECONDS}")
RUN_ENV+=(PERF_DURATION_SECONDS="${DURATION_SECONDS}")
RUN_ENV+=(PERF_TRANSPORT_TRANSITION_MS="${TRANSPORT_TRANSITION_MS}")
RUN_ENV+=(PERF_PATTERN_TRANSITION_MS="${PATTERN_TRANSITION_MS}")
RUN_ENV+=(PERF_SERVER_READY_TIMEOUT_MS="${SERVER_READY_TIMEOUT_MS}")
RUN_ENV+=(PERF_CONNECT_READY_TIMEOUT_MS="${CONNECT_READY_TIMEOUT_MS}")
RUN_ENV+=(PERF_MONITOR_HWM="${MONITOR_HWM}")
RUN_ENV+=(PERF_SERVER_SHUTDOWN_TIMEOUT_MS="${SERVER_SHUTDOWN_TIMEOUT_MS}")
RUN_ENV+=(PERF_SERVER_BIND_PORT="${SERVER_BIND_PORT}")
RUN_ENV+=(PERF_DEFAULT_CLIENTS="${DEFAULT_CLIENTS}")
RUN_ENV+=(PERF_DEFAULT_STREAM_CLIENTS="${DEFAULT_STREAM_CLIENTS}")
RUN_ENV+=(PERF_DEFAULT_HWM="${DEFAULT_HWM}")
RUN_ENV+=(PERF_DEFAULT_STREAM_HWM="${DEFAULT_STREAM_HWM}")
RUN_ENV+=(PERF_DEFAULT_IO_THREADS="${DEFAULT_IO_THREADS}")
RUN_ENV+=(PERF_DEFAULT_STREAM_IO_THREADS="${DEFAULT_STREAM_IO_THREADS}")
if [[ -n "${CLIENTS}" ]]; then
  RUN_ENV+=(PERF_CLIENTS="${CLIENTS}")
fi
if [[ -n "${EXPLICIT_MSG_SIZES}" ]]; then
  RUN_ENV+=(PERF_MSG_SIZES="${EXPLICIT_MSG_SIZES}")
fi
if [[ -n "${EXPLICIT_TRANSPORTS}" ]]; then
  RUN_ENV+=(PERF_TRANSPORTS="${EXPLICIT_TRANSPORTS}")
fi
if [[ -n "${SERVER_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_SERVER_IO_THREADS="${SERVER_IO_THREADS}")
fi
if [[ -n "${CLIENT_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_CLIENT_IO_THREADS="${CLIENT_IO_THREADS}")
fi
if [[ -n "${STREAM_SERVER_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_STREAM_SERVER_IO_THREADS="${STREAM_SERVER_IO_THREADS}")
fi
if [[ -n "${STREAM_CLIENT_IO_THREADS}" ]]; then
  RUN_ENV+=(PERF_STREAM_CLIENT_IO_THREADS="${STREAM_CLIENT_IO_THREADS}")
fi
if [[ -n "${HWM}" ]]; then
  RUN_ENV+=(PERF_HWM="${HWM}")
fi
if [[ -n "${SNDHWM}" ]]; then
  RUN_ENV+=(PERF_SNDHWM="${SNDHWM}")
fi
if [[ -n "${RCVHWM}" ]]; then
  RUN_ENV+=(PERF_RCVHWM="${RCVHWM}")
fi
if [[ -n "${SNDBUF}" ]]; then
  RUN_ENV+=(PERF_SNDBUF="${SNDBUF}")
fi
if [[ -n "${RCVBUF}" ]]; then
  RUN_ENV+=(PERF_RCVBUF="${RCVBUF}")
fi
if [[ -n "${SNDTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_SNDTIMEO_MS="${SNDTIMEO_MS}")
fi
if [[ -n "${RCVTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_RCVTIMEO_MS="${RCVTIMEO_MS}")
fi
if [[ -n "${CONNECT_CONCURRENCY}" ]]; then
  RUN_ENV+=(PERF_CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}")
fi
if [[ "${BUILD_MODE}" == "reuse" ]]; then
  RUN_ENV+=(PERF_NO_AUTOBUILD=1)
fi
if [[ -n "${PERF_DISABLE_RESOURCE_METRICS:-}" ]]; then
  RUN_ENV+=(PERF_DISABLE_RESOURCE_METRICS="${PERF_DISABLE_RESOURCE_METRICS}")
fi

SHOW_TOTAL_TIME=1
FAILED_PATTERNS=()
RUN_PATTERNS=()
RUNNER_PATTERNS=()
SKIPPED_PATTERNS=()

record_skip() {
  local pattern="${1:-}"
  local reason="${2:-skip}"
  SKIPPED_PATTERNS+=("${pattern}: ${reason}")
}

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

print_skip_summary() {
  if [[ "${#SKIPPED_PATTERNS[@]}" -eq 0 ]]; then
    return
  fi
  echo
  echo "## Skips"
  local item=""
  for item in "${SKIPPED_PATTERNS[@]}"; do
    echo "- ${item}"
  done
}

for raw_pattern in "${PATTERNS_TO_RUN[@]}"; do
  pattern="$(printf '%s' "${raw_pattern}" | tr '[:lower:]' '[:upper:]')"
  if [[ "${pattern}" == MULTI_* ]]; then
    pattern="${pattern#MULTI_}"
  fi
  pattern_clients="${CLIENTS:-$(env_or_default "" PERF_CLIENTS PERF_MULTI_CLIENTS)}"
  if [[ -z "${pattern_clients}" ]]; then
    if [[ "${pattern}" == "STREAM" || "${pattern}" == "STREAM_CALLBACK" || "${pattern}" == "STREAM_LEN32BE" ]]; then
      pattern_clients="${DEFAULT_STREAM_CLIENTS}"
    else
      pattern_clients="${DEFAULT_CLIENTS}"
    fi
  fi

  if ! ensure_nofile_limit "${pattern_clients}"; then
    record_skip "${pattern}" "preflight_nofile_${NOFILE_SKIP_REASON}"
    continue
  fi

  if ! ensure_memory_budget "${pattern_clients}"; then
    record_skip "${pattern}" "preflight_memory_${MEMORY_SKIP_REASON}"
    continue
  fi

  RUN_PATTERNS+=("${pattern}")
  RUNNER_PATTERNS+=("MULTI_${pattern}")
done

if [[ "${#RUN_PATTERNS[@]}" -eq 0 ]]; then
  if [[ "${#SKIPPED_PATTERNS[@]}" -eq 0 ]]; then
    echo "Error: no patterns selected to run." >&2
    exit 1
  fi
  print_skip_summary
  exit 0
fi

if [[ -z "${CONNECT_CONCURRENCY}" ]]; then
  max_pattern_clients=0
  for raw_pattern in "${RUN_PATTERNS[@]}"; do
    pattern_clients="${CLIENTS:-}"
    if [[ -z "${pattern_clients}" ]]; then
      if [[ "${raw_pattern}" == "STREAM" || "${raw_pattern}" == "STREAM_CALLBACK" || "${raw_pattern}" == "STREAM_LEN32BE" ]]; then
        pattern_clients="${DEFAULT_STREAM_CLIENTS}"
      else
        pattern_clients="${DEFAULT_CLIENTS}"
      fi
    fi
    if is_uint "${pattern_clients}" && (( pattern_clients > max_pattern_clients )); then
      max_pattern_clients="${pattern_clients}"
    fi
  done
  auto_connect_concurrency="$(resolve_pattern_connect_concurrency "${max_pattern_clients}")"
  if [[ -n "${auto_connect_concurrency}" ]]; then
    RUN_ENV+=(PERF_CONNECT_CONCURRENCY="${auto_connect_concurrency}")
  fi
fi

PATTERN_CSV="$(IFS=,; echo "${RUN_PATTERNS[*]}")"
RUNNER_PATTERN_CSV="$(IFS=,; echo "${RUNNER_PATTERNS[*]}")"
PYTHON_BIN="$(python_bin)"

RUN_CMD=("${PYTHON_BIN}" "${RUNNER_SCRIPT}" "--binding" "${BINDING}" "--suite" "multi" "--pattern" "${RUNNER_PATTERN_CSV}" "--build-dir" "${BUILD_DIR}" "--runs" "${RUNS}" "--results-dir" "${RESULTS_DIR_OVERRIDE}" "--results-tag" "${RESULTS_TAG}" "--result-file" "${RESULT_FILE}")
if [[ "${BUILD_MODE}" == "reuse" ]]; then
  RUN_CMD+=("--reuse-build")
fi
for arg in "${SCRIPT_ARGS[@]}"; do
  RUN_CMD+=("${arg}")
done

echo "=== Running multi benchmark: ${PATTERN_CSV} ==="
echo "    warmup=${WARMUP_SECONDS}s duration=${DURATION_SECONDS}s"

case "${BUILD_MODE}" in
  reuse)
    echo "Reusing build directory: ${BUILD_DIR}"
    ;;
  clean)
    echo "Cleaning build directory: ${BUILD_DIR}"
    ;;
  incremental)
    if [[ -d "${BUILD_DIR}" ]]; then
      echo "Using incremental build directory: ${BUILD_DIR}"
    else
      echo "Creating build directory: ${BUILD_DIR}"
    fi
    ;;
esac

CMAKE_SOURCE_DIR="${ROOT_DIR}"
echo "Using CMake source directory: ${CMAKE_SOURCE_DIR}"

EFFECTIVE_SEND_HWM="${SNDHWM:-${HWM:-}}"
EFFECTIVE_RECV_HWM="${RCVHWM:-${HWM:-}}"
EFFECTIVE_IO_THREADS="default(binary=2)"
if [[ -n "${SERVER_IO_THREADS}" && -n "${CLIENT_IO_THREADS}" && "${SERVER_IO_THREADS}" == "${CLIENT_IO_THREADS}" ]]; then
  EFFECTIVE_IO_THREADS="${SERVER_IO_THREADS}"
fi

echo
echo "## Effective Options (runner)"
print_effective_option "pattern" "${PATTERN_CSV}"
print_effective_option "build_dir" "${BUILD_DIR}"
print_effective_option "build_mode" "${BUILD_MODE}"
print_effective_option "reuse_build" "$( [[ "${BUILD_MODE}" == "reuse" ]] && echo 1 || echo 0 )"
print_effective_option "clean_build" "$( [[ "${BUILD_MODE}" == "clean" ]] && echo 1 || echo 0 )"
print_effective_option "runs" "${RUNS}"
print_effective_option "duration_seconds" "${DURATION_SECONDS}"
print_effective_option "hwm" "$(value_or_default "${HWM}" "default(binary)")"
print_effective_option "send_hwm" "$(value_or_default "${EFFECTIVE_SEND_HWM}" "default(binary)")"
print_effective_option "recv_hwm" "$(value_or_default "${EFFECTIVE_RECV_HWM}" "default(binary)")"
print_effective_option "sndbuf" "$(value_or_default "${SNDBUF}" "default(os)")"
print_effective_option "rcvbuf" "$(value_or_default "${RCVBUF}" "default(os)")"
print_effective_option "sndtimeo_ms" "${SNDTIMEO_MS}"
print_effective_option "rcvtimeo_ms" "${RCVTIMEO_MS}"
print_effective_option "pin_cpu" "${PIN_CPU}"
print_effective_option "io_threads" "${EFFECTIVE_IO_THREADS}"
print_effective_option "msg_sizes" "$(value_or_default "${EXPLICIT_MSG_SIZES:-${PERF_MSG_SIZES:-}}" "default(benchmark)")"
print_effective_option "transports" "$(value_or_default "${EXPLICIT_TRANSPORTS:-${PERF_TRANSPORTS:-}}" "default(benchmark)")"
print_effective_option "results_dir" "${RESULTS_DIR_OVERRIDE}"
print_effective_option "results_tag" "$(value_or_default "${RESULTS_TAG}" "none")"
print_effective_option "result_file" "${RESULT_FILE}"
print_effective_option "output_file" "$(value_or_default "${OUTPUT_FILE}" "none")"
print_effective_option "comparison_script" "${RUNNER_SCRIPT}"
print_effective_option "python" "${PYTHON_BIN}"
echo
echo "## Effective Env (runner)"
VISIBLE_ENV_KEYS=(
  "PYTHONUNBUFFERED"
  "PERF_MSG_SIZES"
  "PERF_TRANSPORTS"
  "PERF_SNDTIMEO_MS"
  "PERF_RCVTIMEO_MS"
  "PERF_NO_AUTOBUILD"
  "PERF_DISABLE_RESOURCE_METRICS"
)
for entry in "${RUN_ENV[@]}"; do
  key="${entry%%=*}"
  value="${entry#*=}"
  if [[ "${key}" == "PERF_OUTPUT_STYLE" ]]; then
    continue
  fi
  for visible_key in "${VISIBLE_ENV_KEYS[@]}"; do
    if [[ "${key}" == "${visible_key}" ]]; then
      print_effective_option "${key}" "${value}"
      break
    fi
  done
done
echo

RUN_EXIT_CODE=0
if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  if PERF_SUPPRESS_TOTAL_TIME=1 env "${RUN_ENV[@]}" "${RUN_CMD[@]}" | tee "${OUTPUT_FILE}"; then
    :
  else
    RUN_EXIT_CODE=$?
    FAILED_PATTERNS+=("${PATTERN_CSV}")
  fi
else
  if PERF_SUPPRESS_TOTAL_TIME=1 env "${RUN_ENV[@]}" "${RUN_CMD[@]}"; then
    :
  else
    RUN_EXIT_CODE=$?
    FAILED_PATTERNS+=("${PATTERN_CSV}")
  fi
fi

if [[ "${#FAILED_PATTERNS[@]}" -gt 0 ]]; then
  print_skip_summary
  echo
  echo "## Failures"
  for pattern in "${FAILED_PATTERNS[@]}"; do
    echo "- ${pattern}"
  done
  exit "${RUN_EXIT_CODE:-1}"
fi

print_skip_summary
