#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MULTI_PATTERNS="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_GATEWAY,MULTI_SPOT,MULTI_STREAM,MULTI_STREAM_CALLBACK,MULTI_STREAM_LEN32BE"
MULTI_TRANSPORTS="tcp,tls,ws,wss"
IFS=',' read -r -a MULTI_PATTERN_LIST <<< "${MULTI_PATTERNS}"

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

is_uint() {
  local value="${1:-}"
  [[ "${value}" =~ ^[0-9]+$ ]]
}

is_nonnegative_number() {
  local value="${1:-}"
  [[ "${value}" =~ ^[0-9]+([.][0-9]+)?$ ]]
}

NOFILE_SKIP_REASON=""
ensure_nofile_limit() {
  local clients="${1:-}"
  NOFILE_SKIP_REASON=""

  if [[ "${BENCH_SKIP_NOFILE_CHECK:-0}" == "1" ]]; then
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

usage() {
  cat <<'USAGE'
Usage: core/perf/run_benchmarks_multi.sh [options]

Run only multi-socket benchmark patterns.
Default PATTERN is:
  MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_GATEWAY,MULTI_SPOT,MULTI_STREAM,MULTI_STREAM_CALLBACK,MULTI_STREAM_LEN32BE
By default, this wrapper runs current zlink only.
By default, multi-bench keeps warmup at 3s and duration window at 5s.
By default, multi-bench uses transports: tcp,tls,ws,wss (can be overridden with --transports).

Modes:
  observe (default)      Collect numbers only (no baseline compare)
  trend                  Compare against rolling baseline (warning only)
  gate                   Compare against fixed baseline (warning + fail)

Options:
  --pattern NAME         Benchmark pattern (default: all MULTI_* patterns above).
  --help                 Show this help.
  --result               Save report result file under core/perf/results/multi/report/ (complete only).
  --save [VER]           Save baseline file under core/perf/results/multi/baseline/ (complete only).
  --results-dir PATH     Override results root directory.
  --results-tag NAME     Optional tag appended to the results filename.
  --build-dir PATH       Override build directory.
  --output PATH          Tee results to a file.
  --runs N               Iterations per configuration (default: 3, gate: 5).
  --reuse-build          Reuse existing build dir without re-running CMake.
  --pin-cpu              Pin CPU core during benchmarks (Linux taskset).
  --io-threads N         Set BENCH_IO_THREADS for the benchmark run.
  --msg-sizes LIST       Comma-separated message sizes.
  --transports LIST      Comma-separated transports.
  --mode MODE            observe | trend | gate (default: observe).
  --rolling-n N          Rolling baseline window size (default: 10).
  --baseline-file PATH   Use explicit fixed baseline file for gate mode.
  --warn-throughput-pct N  Throughput warning drop threshold (default: 10).
  --fail-throughput-pct N  Throughput fail drop threshold (default: 15).
  --warn-latency-pct N     Latency warning increase threshold (default: 10).
  --fail-latency-pct N     Latency fail increase threshold (default: 15).
  --multi-warmup-seconds N
                         Optional override for multi warmup seconds (default 3).
  --multi-duration-seconds N
                         Optional override for multi duration seconds (default 5).
  --multi-clients N      Override number of client sockets per pattern.
  --multi-inflight N     Override max in-flight messages.
  --multi-hwm N          Override BENCH_MULTI_HWM (default: pattern-specific in binary).
  --multi-sndtimeo-ms N  Override BENCH_MULTI_SNDTIMEO_MS (default: 5000).
  --multi-rcvtimeo-ms N  Override BENCH_MULTI_RCVTIMEO_MS (default: 5000).
  --multi-connect-concurrency N
                         Override concurrent connect count.
  --multi-drain-ms N     Override BENCH_MULTI_DRAIN_MS.
  --multi-transport-transition-ms N
                         Override BENCH_MULTI_TRANSPORT_TRANSITION_MS (default: 3000).
  --multi-pattern-transition-ms N
                         Override BENCH_MULTI_PATTERN_TRANSITION_MS (default: 3000).
  --multi-server-ready-timeout-ms N
                         Override BENCH_MULTI_SERVER_READY_TIMEOUT_MS (default: 10000).
  --multi-connect-ready-timeout-ms N
                         Override BENCH_MULTI_CONNECT_READY_TIMEOUT_MS (default: 5000).
  --multi-monitor-hwm N  Override BENCH_MULTI_MONITOR_HWM (default: 200000).
  --multi-server-shutdown-timeout-ms N
                         Override BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS (default: 5000).
  --multi-server-bind-port N
                         Override BENCH_MULTI_SERVER_BIND_PORT (default: 0=auto).

Environment:
  BENCH_SKIP_NOFILE_CHECK=1   Disable preflight nofile(limit) check
  BENCH_MULTI_FORCE_CLEAN=1   Disable default --reuse-build behavior
Notes:
  - tmp result is always saved under results/multi/tmp/.
  - --save-baseline is removed. Use --save [VER].
USAGE
}

pattern_uses_default_drain() {
  local pattern="${1:-}"
  case "${pattern^^}" in
    MULTI_DEALER_DEALER|MULTI_DEALER_ROUTER|MULTI_ROUTER_ROUTER|MULTI_PUBSUB|MULTI_STREAM|MULTI_STREAM_CALLBACK|MULTI_STREAM_LEN32BE)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

resolve_pattern_drain_ms() {
  local pattern="${1:-}"
  if [[ -n "${MULTI_DRAIN_MS}" ]]; then
    echo "${MULTI_DRAIN_MS}"
    return
  fi

  if pattern_uses_default_drain "${pattern}"; then
    echo "${BENCH_MULTI_EXCEPTION_DRAIN_MS:-300}"
  else
    echo "0"
  fi
}

resolve_pattern_connect_concurrency() {
  local clients="${1:-}"
  if [[ -n "${MULTI_CONNECT_CONCURRENCY}" ]]; then
    echo "${MULTI_CONNECT_CONCURRENCY}"
    return
  fi
  if [[ "${clients}" =~ ^[0-9]+$ ]] && (( clients >= 10000 )); then
    echo "1024"
  else
    echo "128"
  fi
}

HAS_EXPLICIT_TRANSPORT=0
HAS_EXPLICIT_MSG_SIZES=0
HAS_EXPLICIT_RESULTS_TAG=0
HAS_EXPLICIT_REUSE_BUILD=0
HAS_EXPLICIT_RUNS=0
HAS_EXPLICIT_RESULTS_DIR=0
MODE="${PERF_MODE:-${BENCH_MODE:-observe}}"
ROLLING_N="${PERF_ROLLING_N:-${BENCH_ROLLING_N:-10}}"
BASELINE_FILE="${PERF_BASELINE_FILE:-${BENCH_BASELINE_FILE:-}}"
WARN_THROUGHPUT_PCT="${PERF_WARN_THROUGHPUT_PCT:-${BENCH_WARN_THROUGHPUT_PCT:-10}}"
FAIL_THROUGHPUT_PCT="${PERF_FAIL_THROUGHPUT_PCT:-${BENCH_FAIL_THROUGHPUT_PCT:-15}}"
WARN_LATENCY_PCT="${PERF_WARN_LATENCY_PCT:-${BENCH_WARN_LATENCY_PCT:-10}}"
FAIL_LATENCY_PCT="${PERF_FAIL_LATENCY_PCT:-${BENCH_FAIL_LATENCY_PCT:-15}}"
MULTI_WARMUP_SECONDS="${PERF_MULTI_WARMUP_SECONDS:-${BENCH_MULTI_WARMUP_SECONDS:-3}}"
MULTI_DURATION_SECONDS="${PERF_MULTI_DURATION_SECONDS:-${BENCH_MULTI_DURATION_SECONDS:-5}}"
MULTI_CLIENTS="${PERF_MULTI_CLIENTS:-${BENCH_MULTI_CLIENTS:-}}"
MULTI_INFLIGHT="${PERF_MULTI_INFLIGHT:-${BENCH_MULTI_INFLIGHT:-}}"
MULTI_HWM="${PERF_MULTI_HWM:-${BENCH_MULTI_HWM:-}}"
MULTI_SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-${BENCH_MULTI_SNDTIMEO_MS:-5000}}"
MULTI_RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-${BENCH_MULTI_RCVTIMEO_MS:-5000}}"
MULTI_CONNECT_CONCURRENCY="${PERF_MULTI_CONNECT_CONCURRENCY:-${BENCH_MULTI_CONNECT_CONCURRENCY:-}}"
MULTI_DRAIN_MS="${PERF_MULTI_DRAIN_MS:-${BENCH_MULTI_DRAIN_MS:-}}"
MULTI_TRANSPORT_TRANSITION_MS="${PERF_MULTI_TRANSPORT_TRANSITION_MS:-${BENCH_MULTI_TRANSPORT_TRANSITION_MS:-3000}}"
MULTI_PATTERN_TRANSITION_MS="${PERF_MULTI_PATTERN_TRANSITION_MS:-${BENCH_MULTI_PATTERN_TRANSITION_MS:-3000}}"
MULTI_SERVER_READY_TIMEOUT_MS="${PERF_MULTI_SERVER_READY_TIMEOUT_MS:-${BENCH_MULTI_SERVER_READY_TIMEOUT_MS:-10000}}"
MULTI_CONNECT_READY_TIMEOUT_MS="${PERF_MULTI_CONNECT_READY_TIMEOUT_MS:-${BENCH_MULTI_CONNECT_READY_TIMEOUT_MS:-5000}}"
MULTI_MONITOR_HWM="${PERF_MULTI_MONITOR_HWM:-${BENCH_MULTI_MONITOR_HWM:-200000}}"
MULTI_SERVER_SHUTDOWN_TIMEOUT_MS="${PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-${BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-5000}}"
MULTI_SERVER_BIND_PORT="${PERF_MULTI_SERVER_BIND_PORT:-${BENCH_MULTI_SERVER_BIND_PORT:-0}}"
RESULTS_DIR_OVERRIDE="${PERF_RESULTS_DIR:-${BENCH_RESULTS_DIR:-}}"
EXPLICIT_PATTERNS=()
SCRIPT_ARGS=()

while [[ $# -gt 0 ]]; do
  arg="$1"
  case "${arg}" in
    -h|--help)
      usage
      exit 0
      ;;
    --transports)
      HAS_EXPLICIT_TRANSPORT=1
      if [[ $# -lt 2 ]]; then
        echo "Error: ${arg} requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --msg-sizes)
      HAS_EXPLICIT_MSG_SIZES=1
      if [[ $# -lt 2 ]]; then
        echo "Error: ${arg} requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --pattern)
      if [[ $# -lt 2 ]]; then
        echo "Error: --pattern requires a value." >&2
        exit 1
      fi
      if [[ "$(printf '%s' "$2" | tr '[:lower:]' '[:upper:]')" == "ALL" ]]; then
        EXPLICIT_PATTERNS=("${MULTI_PATTERN_LIST[@]}")
        shift 2
        continue
      fi
      IFS=',' read -r -a pattern_list <<< "$2"
      for p in "${pattern_list[@]}"; do
        if [[ -n "${p}" ]]; then
          EXPLICIT_PATTERNS+=( "${p}" )
        fi
      done
      shift 2
      ;;
    --mode)
      if [[ $# -lt 2 ]]; then
        echo "Error: --mode requires a value." >&2
        exit 1
      fi
      MODE="${2}"
      shift 2
      ;;
    --rolling-n)
      if [[ $# -lt 2 ]]; then
        echo "Error: --rolling-n requires a value." >&2
        exit 1
      fi
      ROLLING_N="${2}"
      shift 2
      ;;
    --baseline-file)
      if [[ $# -lt 2 ]]; then
        echo "Error: --baseline-file requires a value." >&2
        exit 1
      fi
      BASELINE_FILE="${2}"
      shift 2
      ;;
    --warn-throughput-pct)
      if [[ $# -lt 2 ]]; then
        echo "Error: --warn-throughput-pct requires a value." >&2
        exit 1
      fi
      WARN_THROUGHPUT_PCT="${2}"
      shift 2
      ;;
    --fail-throughput-pct)
      if [[ $# -lt 2 ]]; then
        echo "Error: --fail-throughput-pct requires a value." >&2
        exit 1
      fi
      FAIL_THROUGHPUT_PCT="${2}"
      shift 2
      ;;
    --warn-latency-pct)
      if [[ $# -lt 2 ]]; then
        echo "Error: --warn-latency-pct requires a value." >&2
        exit 1
      fi
      WARN_LATENCY_PCT="${2}"
      shift 2
      ;;
    --fail-latency-pct)
      if [[ $# -lt 2 ]]; then
        echo "Error: --fail-latency-pct requires a value." >&2
        exit 1
      fi
      FAIL_LATENCY_PCT="${2}"
      shift 2
      ;;
    --results-tag)
      HAS_EXPLICIT_RESULTS_TAG=1
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --reuse-build)
      HAS_EXPLICIT_REUSE_BUILD=1
      SCRIPT_ARGS+=( "$1" )
      shift
      ;;
    --runs)
      HAS_EXPLICIT_RUNS=1
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --runs=*)
      HAS_EXPLICIT_RUNS=1
      SCRIPT_ARGS+=( "$1" )
      shift
      ;;
    --multi-warmup-seconds)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_WARMUP_SECONDS="${2}"
      shift 2
      ;;
    --multi-duration-seconds)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_DURATION_SECONDS="${2}"
      shift 2
      ;;
    --result|--pin-cpu)
      SCRIPT_ARGS+=( "$1" )
      shift
      ;;
    --save)
      SCRIPT_ARGS+=( "$1" )
      if [[ $# -ge 2 && "${2:-}" != --* ]]; then
        SCRIPT_ARGS+=( "$2" )
        shift 2
      else
        shift
      fi
      ;;
    --results-dir)
      HAS_EXPLICIT_RESULTS_DIR=1
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      RESULTS_DIR_OVERRIDE="${2}"
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --build-dir|--output|--io-threads)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      SCRIPT_ARGS+=( "$1" "$2" )
      shift 2
      ;;
    --multi-clients)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_CLIENTS="${2}"
      shift 2
      ;;
    --multi-inflight)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_INFLIGHT="${2}"
      shift 2
      ;;
    --multi-hwm)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_HWM="${2}"
      shift 2
      ;;
    --multi-sndtimeo-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_SNDTIMEO_MS="${2}"
      shift 2
      ;;
    --multi-rcvtimeo-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_RCVTIMEO_MS="${2}"
      shift 2
      ;;
    --multi-connect-concurrency)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_CONNECT_CONCURRENCY="${2}"
      shift 2
      ;;
    --multi-drain-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_DRAIN_MS="${2}"
      shift 2
      ;;
    --multi-transport-transition-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_TRANSPORT_TRANSITION_MS="${2}"
      shift 2
      ;;
    --multi-pattern-transition-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_PATTERN_TRANSITION_MS="${2}"
      shift 2
      ;;
    --multi-server-ready-timeout-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_SERVER_READY_TIMEOUT_MS="${2}"
      shift 2
      ;;
    --multi-connect-ready-timeout-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_CONNECT_READY_TIMEOUT_MS="${2}"
      shift 2
      ;;
    --multi-monitor-hwm)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_MONITOR_HWM="${2}"
      shift 2
      ;;
    --multi-server-shutdown-timeout-ms)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_SERVER_SHUTDOWN_TIMEOUT_MS="${2}"
      shift 2
      ;;
    --multi-server-bind-port)
      if [[ $# -lt 2 ]]; then
        echo "Error: $1 requires a value." >&2
        exit 1
      fi
      MULTI_SERVER_BIND_PORT="${2}"
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

MODE="$(printf '%s' "${MODE}" | tr '[:upper:]' '[:lower:]')"
case "${MODE}" in
  observe|trend|gate)
    ;;
  *)
    echo "Error: --mode must be one of observe|trend|gate." >&2
    exit 1
    ;;
esac

if ! is_uint "${ROLLING_N}" || (( ROLLING_N < 1 )); then
  echo "Error: --rolling-n must be a positive integer." >&2
  exit 1
fi

if ! is_nonnegative_number "${WARN_THROUGHPUT_PCT}"; then
  echo "Error: --warn-throughput-pct must be a non-negative number." >&2
  exit 1
fi
if ! is_nonnegative_number "${FAIL_THROUGHPUT_PCT}"; then
  echo "Error: --fail-throughput-pct must be a non-negative number." >&2
  exit 1
fi
if ! is_nonnegative_number "${WARN_LATENCY_PCT}"; then
  echo "Error: --warn-latency-pct must be a non-negative number." >&2
  exit 1
fi
if ! is_nonnegative_number "${FAIL_LATENCY_PCT}"; then
  echo "Error: --fail-latency-pct must be a non-negative number." >&2
  exit 1
fi
if ! is_uint "${MULTI_TRANSPORT_TRANSITION_MS}"; then
  echo "Error: --multi-transport-transition-ms must be a non-negative integer." >&2
  exit 1
fi
if ! is_uint "${MULTI_PATTERN_TRANSITION_MS}"; then
  echo "Error: --multi-pattern-transition-ms must be a non-negative integer." >&2
  exit 1
fi
if ! is_uint "${MULTI_SERVER_READY_TIMEOUT_MS}"; then
  echo "Error: --multi-server-ready-timeout-ms must be a non-negative integer." >&2
  exit 1
fi
if ! is_uint "${MULTI_CONNECT_READY_TIMEOUT_MS}"; then
  echo "Error: --multi-connect-ready-timeout-ms must be a non-negative integer." >&2
  exit 1
fi
if ! is_uint "${MULTI_MONITOR_HWM}"; then
  echo "Error: --multi-monitor-hwm must be a non-negative integer." >&2
  exit 1
fi
if ! is_uint "${MULTI_SERVER_SHUTDOWN_TIMEOUT_MS}"; then
  echo "Error: --multi-server-shutdown-timeout-ms must be a non-negative integer." >&2
  exit 1
fi
if ! is_uint "${MULTI_SERVER_BIND_PORT}" || (( MULTI_SERVER_BIND_PORT > 65535 )); then
  echo "Error: --multi-server-bind-port must be an integer in range 0..65535." >&2
  exit 1
fi

if [[ -n "${BASELINE_FILE}" && "${MODE}" != "gate" ]]; then
  echo "Warning: --baseline-file is used only in gate mode; current mode=${MODE}." >&2
fi

PATTERNS=("${MULTI_PATTERN_LIST[@]}")
if [[ "${#EXPLICIT_PATTERNS[@]}" -gt 0 ]]; then
  PATTERNS=("${EXPLICIT_PATTERNS[@]}")
fi

RUN_BASE_ARGS=()
if [[ "${HAS_EXPLICIT_TRANSPORT}" -eq 0 ]]; then
  if [[ -n "${PERF_TRANSPORTS:-}" ]]; then
    RUN_BASE_ARGS+=(--transports "${PERF_TRANSPORTS}")
  elif [[ -n "${BENCH_TRANSPORTS:-}" ]]; then
    RUN_BASE_ARGS+=(--transports "${BENCH_TRANSPORTS}")
  else
    RUN_BASE_ARGS+=(--transports "${MULTI_TRANSPORTS}")
  fi
fi
if [[ "${HAS_EXPLICIT_MSG_SIZES}" -eq 0 ]]; then
  if [[ -n "${PERF_MSG_SIZES:-}" ]]; then
    RUN_BASE_ARGS+=(--msg-sizes "${PERF_MSG_SIZES}")
  elif [[ -n "${BENCH_MSG_SIZES:-}" ]]; then
    RUN_BASE_ARGS+=(--msg-sizes "${BENCH_MSG_SIZES}")
  fi
fi
if [[ "${HAS_EXPLICIT_REUSE_BUILD}" -eq 0 && "${PERF_MULTI_FORCE_CLEAN:-${BENCH_MULTI_FORCE_CLEAN:-0}}" != "1" ]]; then
  RUN_BASE_ARGS+=(--reuse-build)
fi
if [[ "${HAS_EXPLICIT_RUNS}" -eq 0 ]]; then
  if [[ "${MODE}" == "gate" ]]; then
    RUN_BASE_ARGS+=(--runs "5")
  else
    RUN_BASE_ARGS+=(--runs "3")
  fi
fi
RUN_BASE_ARGS+=(--mode "${MODE}" --rolling-n "${ROLLING_N}")
if [[ -n "${BASELINE_FILE}" ]]; then
  RUN_BASE_ARGS+=(--baseline-file "${BASELINE_FILE}")
fi

if [[ -z "${RESULTS_DIR_OVERRIDE}" ]]; then
  RESULTS_DIR_OVERRIDE="${SCRIPT_DIR}/results"
fi

RUN_ENV=()
RUN_ENV+=(PERF_ALLOW_MULTI="1")
RUN_ENV+=(PERF_MULTI_POLICY="1")
RUN_ENV+=(PERF_MODE="${MODE}")
RUN_ENV+=(PERF_ROLLING_N="${ROLLING_N}")
RUN_ENV+=(PERF_WARN_THROUGHPUT_PCT="${WARN_THROUGHPUT_PCT}")
RUN_ENV+=(PERF_FAIL_THROUGHPUT_PCT="${FAIL_THROUGHPUT_PCT}")
RUN_ENV+=(PERF_WARN_LATENCY_PCT="${WARN_LATENCY_PCT}")
RUN_ENV+=(PERF_FAIL_LATENCY_PCT="${FAIL_LATENCY_PCT}")
RUN_ENV+=(PERF_RESULTS_DIR="${RESULTS_DIR_OVERRIDE}")
RUN_ENV+=(PERF_MULTI_WARMUP_SECONDS="${MULTI_WARMUP_SECONDS}")
RUN_ENV+=(PERF_MULTI_DURATION_SECONDS="${MULTI_DURATION_SECONDS}")
RUN_ENV+=(PERF_MULTI_TRANSPORT_TRANSITION_MS="${MULTI_TRANSPORT_TRANSITION_MS}")
RUN_ENV+=(PERF_MULTI_PATTERN_TRANSITION_MS="${MULTI_PATTERN_TRANSITION_MS}")
RUN_ENV+=(PERF_MULTI_SERVER_READY_TIMEOUT_MS="${MULTI_SERVER_READY_TIMEOUT_MS}")
RUN_ENV+=(PERF_MULTI_CONNECT_READY_TIMEOUT_MS="${MULTI_CONNECT_READY_TIMEOUT_MS}")
RUN_ENV+=(PERF_MULTI_MONITOR_HWM="${MULTI_MONITOR_HWM}")
RUN_ENV+=(PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS="${MULTI_SERVER_SHUTDOWN_TIMEOUT_MS}")
RUN_ENV+=(PERF_MULTI_SERVER_BIND_PORT="${MULTI_SERVER_BIND_PORT}")
RUN_ENV+=(BENCH_MULTI_POLICY="1")
RUN_ENV+=(BENCH_MODE="${MODE}")
RUN_ENV+=(BENCH_ROLLING_N="${ROLLING_N}")
RUN_ENV+=(BENCH_WARN_THROUGHPUT_PCT="${WARN_THROUGHPUT_PCT}")
RUN_ENV+=(BENCH_FAIL_THROUGHPUT_PCT="${FAIL_THROUGHPUT_PCT}")
RUN_ENV+=(BENCH_WARN_LATENCY_PCT="${WARN_LATENCY_PCT}")
RUN_ENV+=(BENCH_FAIL_LATENCY_PCT="${FAIL_LATENCY_PCT}")
RUN_ENV+=(BENCH_RESULTS_DIR="${RESULTS_DIR_OVERRIDE}")
RUN_ENV+=(BENCH_MULTI_WARMUP_SECONDS="${MULTI_WARMUP_SECONDS}")
RUN_ENV+=(BENCH_MULTI_DURATION_SECONDS="${MULTI_DURATION_SECONDS}")
RUN_ENV+=(BENCH_MULTI_TRANSPORT_TRANSITION_MS="${MULTI_TRANSPORT_TRANSITION_MS}")
RUN_ENV+=(BENCH_MULTI_PATTERN_TRANSITION_MS="${MULTI_PATTERN_TRANSITION_MS}")
RUN_ENV+=(BENCH_MULTI_SERVER_READY_TIMEOUT_MS="${MULTI_SERVER_READY_TIMEOUT_MS}")
RUN_ENV+=(BENCH_MULTI_CONNECT_READY_TIMEOUT_MS="${MULTI_CONNECT_READY_TIMEOUT_MS}")
RUN_ENV+=(BENCH_MULTI_MONITOR_HWM="${MULTI_MONITOR_HWM}")
RUN_ENV+=(BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS="${MULTI_SERVER_SHUTDOWN_TIMEOUT_MS}")
RUN_ENV+=(BENCH_MULTI_SERVER_BIND_PORT="${MULTI_SERVER_BIND_PORT}")
if [[ -n "${BASELINE_FILE}" ]]; then
  RUN_ENV+=(PERF_BASELINE_FILE="${BASELINE_FILE}")
  RUN_ENV+=(BENCH_BASELINE_FILE="${BASELINE_FILE}")
fi
if [[ -n "${MULTI_CLIENTS}" ]]; then
  RUN_ENV+=(PERF_MULTI_CLIENTS="${MULTI_CLIENTS}")
  RUN_ENV+=(BENCH_MULTI_CLIENTS="${MULTI_CLIENTS}")
fi
if [[ -n "${MULTI_INFLIGHT}" ]]; then
  RUN_ENV+=(PERF_MULTI_INFLIGHT="${MULTI_INFLIGHT}")
  RUN_ENV+=(BENCH_MULTI_INFLIGHT="${MULTI_INFLIGHT}")
fi
if [[ -n "${MULTI_HWM}" ]]; then
  RUN_ENV+=(PERF_MULTI_HWM="${MULTI_HWM}")
  RUN_ENV+=(BENCH_MULTI_HWM="${MULTI_HWM}")
fi
if [[ -n "${MULTI_SNDTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_MULTI_SNDTIMEO_MS="${MULTI_SNDTIMEO_MS}")
  RUN_ENV+=(BENCH_MULTI_SNDTIMEO_MS="${MULTI_SNDTIMEO_MS}")
fi
if [[ -n "${MULTI_RCVTIMEO_MS}" ]]; then
  RUN_ENV+=(PERF_MULTI_RCVTIMEO_MS="${MULTI_RCVTIMEO_MS}")
  RUN_ENV+=(BENCH_MULTI_RCVTIMEO_MS="${MULTI_RCVTIMEO_MS}")
fi

if [[ "${HAS_EXPLICIT_RESULTS_DIR}" -eq 0 && -n "${PERF_RESULTS_DIR:-${BENCH_RESULTS_DIR:-}}" ]]; then
  RUN_ENV+=(PERF_RESULTS_DIR="${PERF_RESULTS_DIR:-${BENCH_RESULTS_DIR:-}}")
  RUN_ENV+=(BENCH_RESULTS_DIR="${PERF_RESULTS_DIR:-${BENCH_RESULTS_DIR:-}}")
fi

SHOW_TOTAL_TIME=1
FAILED_PATTERNS=()
SKIPPED_PATTERNS=()
RUN_PATTERNS=()
for raw_pattern in "${PATTERNS[@]}"; do
  pattern="$(printf '%s' "${raw_pattern}" | tr '[:lower:]' '[:upper:]')"
  if [[ "${pattern}" == "MULTI_ROUTER_ROUTER_POLL" ]]; then
    echo "Error: MULTI_ROUTER_ROUTER_POLL is removed from multi benchmarks." >&2
    exit 1
  fi
  if [[ "${pattern}" != MULTI_* ]]; then
    echo "Error: run_benchmarks_multi.sh accepts only MULTI_* patterns." >&2
    exit 1
  fi

  pattern_clients="${MULTI_CLIENTS:-${PERF_MULTI_CLIENTS:-${BENCH_MULTI_CLIENTS:-}}}"
  if [[ -z "${pattern_clients}" ]]; then
    if [[ "${pattern}" == "MULTI_STREAM" || "${pattern}" == "MULTI_STREAM_CALLBACK" || "${pattern}" == "MULTI_STREAM_LEN32BE" ]]; then
      pattern_clients="${PERF_MULTI_DEFAULT_STREAM_CLIENTS:-${BENCH_MULTI_DEFAULT_STREAM_CLIENTS:-1000}}"
    else
      pattern_clients="${PERF_MULTI_DEFAULT_CLIENTS:-${BENCH_MULTI_DEFAULT_CLIENTS:-1000}}"
    fi
  fi

  if ! ensure_nofile_limit "${pattern_clients}"; then
    echo "SKIP,current,${pattern},all,${NOFILE_SKIP_REASON}" >&2
    SKIPPED_PATTERNS+=("${pattern}")
    continue
  fi

  RUN_PATTERNS+=("${pattern}")
done

if [[ "${#RUN_PATTERNS[@]}" -eq 0 ]]; then
  if [[ "${#SKIPPED_PATTERNS[@]}" -gt 0 ]]; then
    echo
    echo "## Skips"
    for pattern in "${SKIPPED_PATTERNS[@]}"; do
      echo "- ${pattern}: nofile preflight unmet"
    done
  fi
  exit 0
fi

if [[ -n "${MULTI_CONNECT_CONCURRENCY}" ]]; then
  RUN_ENV+=(PERF_MULTI_CONNECT_CONCURRENCY="${MULTI_CONNECT_CONCURRENCY}")
  RUN_ENV+=(BENCH_MULTI_CONNECT_CONCURRENCY="${MULTI_CONNECT_CONCURRENCY}")
fi
if [[ -n "${MULTI_DRAIN_MS}" ]]; then
  RUN_ENV+=(PERF_MULTI_DRAIN_MS="${MULTI_DRAIN_MS}")
  RUN_ENV+=(BENCH_MULTI_DRAIN_MS="${MULTI_DRAIN_MS}")
fi

PATTERN_CSV="$(IFS=,; echo "${RUN_PATTERNS[*]}")"
echo "=== Running multi benchmark: ${PATTERN_CSV} ==="
echo "    mode=${MODE} warmup=${MULTI_WARMUP_SECONDS}s duration=${MULTI_DURATION_SECONDS}s"
RUN_EXIT_CODE=0
if BENCH_ALLOW_MULTI=1 \
  PERF_ALLOW_MULTI=1 \
  BENCH_SUPPRESS_TOTAL_TIME=1 \
  PERF_SUPPRESS_TOTAL_TIME=1 \
  env "${RUN_ENV[@]}" \
  "${SCRIPT_DIR}/run_benchmarks.sh" \
  "${RUN_BASE_ARGS[@]}" \
  "${SCRIPT_ARGS[@]}" \
  --pattern "${PATTERN_CSV}"; then
  :
else
  RUN_EXIT_CODE=$?
  FAILED_PATTERNS+=("${PATTERN_CSV}")
fi

if [[ "${#SKIPPED_PATTERNS[@]}" -gt 0 ]]; then
  echo
  echo "## Skips"
  for pattern in "${SKIPPED_PATTERNS[@]}"; do
    echo "- ${pattern}: nofile preflight unmet"
  done
fi

if [[ "${#FAILED_PATTERNS[@]}" -gt 0 ]]; then
  echo
  echo "## Failures"
  for pattern in "${FAILED_PATTERNS[@]}"; do
    echo "- ${pattern}"
  done
  if [[ "${RUN_EXIT_CODE}" -eq 2 ]]; then
    exit 2
  fi
  exit 1
fi
