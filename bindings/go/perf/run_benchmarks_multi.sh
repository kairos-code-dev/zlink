#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${ROOT_DIR}/../.." && pwd)"
cd "${ROOT_DIR}"

export GOCACHE="${GOCACHE:-/tmp/zlink-go-cache}"
export GOTMPDIR="${GOTMPDIR:-/tmp/zlink-go-tmp}"
mkdir -p "${GOCACHE}" "${GOTMPDIR}"

VERSION_FILE="${REPO_DIR}/VERSION"
CORE_LIB_DIR="${REPO_DIR}/core/build/lib"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so.${CORE_VERSION}"

PATTERN="ALL"
DURATION="5"
MSG_SIZES=""
TRANSPORTS="${PERF_TRANSPORTS:-}"
RUNS="1"
CLIENTS=""
RESULTS_DIR="${SCRIPT_DIR}/results/multi/report"
RESULTS_TAG=""
OUTPUT_FILE=""
PIN_CPU="off"
IO_THREADS=""
SERVER_IO_THREADS=""
CLIENT_IO_THREADS=""
HWM=""
SEND_HWM=""
RECV_HWM=""
SNDBUF=""
RCVBUF=""
SNDTIMEO_MS=""
RCVTIMEO_MS=""
AUTO_HWM_PROFILE=""
CONNECT_READY_TIMEOUT_MS=""
CONNECT_CONCURRENCY=""
SERVER_READY_TIMEOUT_MS=""
MONITOR_HWM=""
SERVER_SHUTDOWN_TIMEOUT_MS=""
SERVER_BIND_PORT=""
RUN_COOLDOWN_MS="${PERF_MULTI_RUN_COOLDOWN_MS:-3000}"
CASE_RETRIES="${PERF_MULTI_CASE_RETRIES:-3}"
TRANSPORT_TRANSITION_MS="${PERF_MULTI_TRANSPORT_TRANSITION_MS:-3000}"
PATTERN_TRANSITION_MS="${PERF_MULTI_PATTERN_TRANSITION_MS:-3000}"

cleanup_report_dir() {
  local dir="$1"
  local max_files="${PERF_RESULTS_MAX_FILES:-100}"
  mkdir -p "${dir}"
  mapfile -t existing < <(find "${dir}" -maxdepth 1 -type f -name 'perf_go_multi_*.txt' | sort)
  while [[ "${#existing[@]}" -ge "${max_files}" ]]; do
    rm -f "${existing[0]}"
    existing=("${existing[@]:1}")
  done
}

resolve_results_dir() {
  local dir="$1"
  case "${dir}" in
    */multi/report|*/report)
      echo "${dir}"
      ;;
    *)
      echo "${dir}/multi/report"
      ;;
  esac
}

sleep_millis() {
  local millis="${1:-0}"
  if [[ ! "${millis}" =~ ^[0-9]+$ || "${millis}" -eq 0 ]]; then
    return
  fi
  sleep "$((millis / 1000)).$(printf '%03d' "$((millis % 1000))")"
}

usage() {
  cat <<'USAGE'
Usage: bindings/go/perf/run_benchmarks_multi.sh [options]

Options:
  --pattern NAME
  --duration N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --clients N
  --results-dir PATH
  --results-tag NAME
  --output PATH
  --build-dir PATH
  --reuse-build
  --clean-build
  --pin-cpu
  --io-threads N
  --server-io-threads N
  --client-io-threads N
  --hwm N
  --send-hwm N
  --recv-hwm N
  --buf SIZE
  --sndbuf SIZE
  --rcvbuf SIZE
  --sndtimeo N
  --rcvtimeo N
  --send-timeout-ms N
  --recv-timeout-ms N
  --connect-concurrency N
  --transport-transition-ms N
  --pattern-transition-ms N
  --server-ready-timeout-ms N
  --connect-ready-timeout-ms N
  --monitor-hwm N
  --server-shutdown-timeout-ms N
  --server-bind-port N
  --auto-hwm-profile NAME
  -h, --help

Notes:
  - Supported multi patterns: MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND,MULTI_STREAM
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
    --transports) TRANSPORTS="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --clients) CLIENTS="$2"; shift 2 ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --results-tag) RESULTS_TAG="$2"; shift 2 ;;
    --output) OUTPUT_FILE="$2"; shift 2 ;;
    --build-dir)
      shift 2 ;;
    --io-threads)
      IO_THREADS="$2"
      shift 2 ;;
    --server-io-threads)
      SERVER_IO_THREADS="$2"
      shift 2 ;;
    --client-io-threads)
      CLIENT_IO_THREADS="$2"
      shift 2 ;;
    --connect-concurrency)
      CONNECT_CONCURRENCY="$2"
      shift 2 ;;
    --server-ready-timeout-ms)
      SERVER_READY_TIMEOUT_MS="$2"
      shift 2 ;;
    --monitor-hwm)
      MONITOR_HWM="$2"
      shift 2 ;;
    --server-shutdown-timeout-ms)
      SERVER_SHUTDOWN_TIMEOUT_MS="$2"
      shift 2 ;;
    --server-bind-port)
      SERVER_BIND_PORT="$2"
      shift 2 ;;
    --hwm)
      HWM="$2"
      shift 2 ;;
    --send-hwm)
      SEND_HWM="$2"
      shift 2 ;;
    --recv-hwm)
      RECV_HWM="$2"
      shift 2 ;;
    --buf)
      SNDBUF="$2"
      RCVBUF="$2"
      shift 2 ;;
    --sndbuf)
      SNDBUF="$2"
      shift 2 ;;
    --rcvbuf)
      RCVBUF="$2"
      shift 2 ;;
    --sndtimeo|--send-timeout-ms)
      SNDTIMEO_MS="$2"
      shift 2 ;;
    --rcvtimeo|--recv-timeout-ms)
      RCVTIMEO_MS="$2"
      shift 2 ;;
    --connect-ready-timeout-ms)
      CONNECT_READY_TIMEOUT_MS="$2"
      shift 2 ;;
    --transport-transition-ms)
      TRANSPORT_TRANSITION_MS="$2"
      shift 2 ;;
    --pattern-transition-ms)
      PATTERN_TRANSITION_MS="$2"
      shift 2 ;;
    --auto-hwm-profile)
      AUTO_HWM_PROFILE="$2"
      shift 2 ;;
    --reuse-build|--clean-build)
      shift ;;
    --pin-cpu)
      PIN_CPU="on"
      shift ;;
    *)
      echo "Error: unknown option $1" >&2
      exit 1 ;;
  esac
done

if [[ -n "${HWM}" ]]; then
  export PERF_MULTI_HWM="${HWM}"
fi
if [[ -n "${IO_THREADS}" ]]; then
  export PERF_IO_THREADS="${IO_THREADS}"
fi
if [[ -n "${SERVER_IO_THREADS}" ]]; then
  export PERF_MULTI_SERVER_IO_THREADS="${SERVER_IO_THREADS}"
fi
if [[ -n "${CLIENT_IO_THREADS}" ]]; then
  export PERF_MULTI_CLIENT_IO_THREADS="${CLIENT_IO_THREADS}"
fi
if [[ -n "${SEND_HWM}" ]]; then
  export PERF_MULTI_SNDHWM="${SEND_HWM}"
fi
if [[ -n "${RECV_HWM}" ]]; then
  export PERF_MULTI_RCVHWM="${RECV_HWM}"
fi
if [[ -n "${SNDBUF}" ]]; then
  export PERF_MULTI_SNDBUF="${SNDBUF}"
fi
if [[ -n "${RCVBUF}" ]]; then
  export PERF_MULTI_RCVBUF="${RCVBUF}"
fi
if [[ -n "${SNDTIMEO_MS}" ]]; then
  export PERF_MULTI_SNDTIMEO_MS="${SNDTIMEO_MS}"
fi
if [[ -n "${RCVTIMEO_MS}" ]]; then
  export PERF_MULTI_RCVTIMEO_MS="${RCVTIMEO_MS}"
fi
if [[ -n "${CONNECT_READY_TIMEOUT_MS}" ]]; then
  export PERF_MULTI_CONNECT_READY_TIMEOUT_MS="${CONNECT_READY_TIMEOUT_MS}"
fi
if [[ -n "${AUTO_HWM_PROFILE}" ]]; then
  export PERF_CTX_AUTO_HWM_PROFILE="${AUTO_HWM_PROFILE}"
fi
if [[ -n "${CONNECT_CONCURRENCY}" ]]; then
  export PERF_MULTI_CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}"
fi
if [[ -n "${SERVER_READY_TIMEOUT_MS}" ]]; then
  export PERF_MULTI_SERVER_READY_TIMEOUT_MS="${SERVER_READY_TIMEOUT_MS}"
fi
if [[ -n "${MONITOR_HWM}" ]]; then
  export PERF_MULTI_MONITOR_HWM="${MONITOR_HWM}"
fi
if [[ -n "${SERVER_SHUTDOWN_TIMEOUT_MS}" ]]; then
  export PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS="${SERVER_SHUTDOWN_TIMEOUT_MS}"
fi
if [[ -n "${SERVER_BIND_PORT}" ]]; then
  export PERF_MULTI_SERVER_BIND_PORT="${SERVER_BIND_PORT}"
fi

case "$(uname -s)" in
  Linux*) PLATFORM="linux" ;;
  Darwin*) PLATFORM="macos" ;;
  *) PLATFORM="windows" ;;
esac

run_go_perf() {
  if [[ "${PIN_CPU}" != "on" ]]; then
    go run "$@"
    return
  fi

  case "$(uname -s)" in
    Linux*)
      if ! command -v taskset >/dev/null 2>&1; then
        echo "Error: --pin-cpu requires taskset on Linux" >&2
        return 1
      fi
      taskset -c 0 go run "$@"
      ;;
    *)
      echo "Error: --pin-cpu is not supported by this runner on $(uname -s)" >&2
      return 1
      ;;
  esac
}

prepare_core_runtime() {
  if [[ ! -f "${CORE_LIB}" ]]; then
    echo "core runtime not found: ${CORE_LIB}" >&2
    echo "Build core/build before running Go perf." >&2
    exit 1
  fi
  export LD_LIBRARY_PATH="${CORE_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
}

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""
if [[ -n "${RESULTS_TAG}" ]]; then
  TAG_SUFFIX="_${RESULTS_TAG}"
fi
RESULTS_DIR="$(resolve_results_dir "${RESULTS_DIR}")"
RESULTS_FILE="${RESULTS_DIR}/perf_go_multi_${PLATFORM}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"
cleanup_report_dir "${RESULTS_DIR}"
prepare_core_runtime
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-go-multi.XXXXXX")"
cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

normalize_multi_pattern() {
  local raw="$1"
  raw="${raw^^}"
  if [[ "${raw}" == MULTI_* ]]; then
    echo "${raw}"
  else
    echo "MULTI_${raw}"
  fi
}

pattern_msg_sizes() {
  local pattern="$1"
  if [[ -n "${MSG_SIZES}" ]]; then
    echo "${MSG_SIZES}"
    return
  fi
  if [[ "${pattern}" == "MULTI_STREAM" ]]; then
    if [[ -n "${PERF_MULTI_STREAM_MSG_SIZES:-}" ]]; then
      echo "${PERF_MULTI_STREAM_MSG_SIZES}"
    elif [[ -n "${PERF_MSG_SIZES:-}" ]]; then
      echo "${PERF_MSG_SIZES}"
    else
      echo "64,256,1024,65536"
    fi
    return
  fi
  if [[ -n "${PERF_MSG_SIZES:-}" ]]; then
    echo "${PERF_MSG_SIZES}"
  else
    echo "64,256,1024,65536,131072,262144"
  fi
}

if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERNS=("MULTI_DEALER_DEALER" "MULTI_DEALER_ROUTER" "MULTI_ROUTER_ROUTER" "MULTI_PUBSUB" "MULTI_SPOT" "MULTI_SPOT_REQREP" "MULTI_SPOT_SENDSEND" "MULTI_STREAM")
else
  IFS=',' read -r -a RAW_PATTERNS <<< "${PATTERN}"
  PATTERNS=()
  for raw_pattern in "${RAW_PATTERNS[@]}"; do
    PATTERNS+=("$(normalize_multi_pattern "${raw_pattern}")")
  done
fi

if [[ -n "${TRANSPORTS}" ]]; then
  IFS=',' read -r -a XPORTS_FILTER <<< "${TRANSPORTS}"
else
  XPORTS_FILTER=()
fi

pattern_transports() {
  case "$1" in
    MULTI_STREAM|MULTI_SPOT|MULTI_SPOT_REQREP|MULTI_SPOT_SENDSEND|MULTI_PUBSUB|MULTI_DEALER_DEALER|MULTI_DEALER_ROUTER|MULTI_ROUTER_ROUTER)
      echo "tcp tls ws wss"
      ;;
    *)
      echo "tcp tls ws wss"
      ;;
  esac
}

transport_enabled() {
  local transport="$1"
  if [[ "${#XPORTS_FILTER[@]}" -eq 0 ]]; then
    return 0
  fi
  local candidate
  for candidate in "${XPORTS_FILTER[@]}"; do
    if [[ "${candidate}" == "${transport}" ]]; then
      return 0
    fi
  done
  return 1
}

join_by() {
  local delim="$1"
  shift
  local first=1
  local item
  for item in "$@"; do
    if [[ "${first}" -eq 1 ]]; then
      printf '%s' "${item}"
      first=0
    else
      printf '%s%s' "${delim}" "${item}"
    fi
  done
}

resolve_multi_effective_transports() {
  declare -A seen=()
  local pattern transport
  local resolved=()
  for pattern in "${PATTERNS[@]}"; do
    read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
    for transport in "${PATTERN_XPORTS[@]}"; do
      if ! transport_enabled "${transport}"; then
        continue
      fi
      if [[ -n "${seen[${transport}]:-}" ]]; then
        continue
      fi
      seen["${transport}"]=1
      resolved+=("${transport}")
    done
  done
  join_by "," "${resolved[@]}"
}

resolve_multi_effective_msg_sizes() {
  declare -A seen=()
  local pattern size_list size
  local resolved=()
  for pattern in "${PATTERNS[@]}"; do
    size_list="$(pattern_msg_sizes "${pattern}")"
    IFS=',' read -r -a SIZE_ITEMS <<< "${size_list}"
    for size in "${SIZE_ITEMS[@]}"; do
      if [[ -n "${seen[${size}]:-}" ]]; then
        continue
      fi
      seen["${size}"]=1
      resolved+=("${size}")
    done
  done
  join_by "," "${resolved[@]}"
}

EFFECTIVE_PATTERNS_CSV="$(join_by "," "${PATTERNS[@]}")"
EFFECTIVE_TRANSPORTS_CSV="$(resolve_multi_effective_transports)"
EFFECTIVE_MSG_SIZES_CSV="$(resolve_multi_effective_msg_sizes)"
if [[ -n "${CLIENTS}" ]]; then
  CLIENTS_DISPLAY="${CLIENTS}"
elif [[ -n "${PERF_MULTI_CLIENTS:-}" ]]; then
  CLIENTS_DISPLAY="${PERF_MULTI_CLIENTS}"
else
  CLIENTS_DISPLAY="auto (default=100, stream=10000)"
fi

is_unsupported_output() {
  local output_file="$1"
  grep -Eiq 'protocol not supported' "${output_file}"
}

append_case_output() {
  local case_log="$1"
  cat "${case_log}" >> "${RESULTS_FILE}"
  if [[ -s "${case_log}" ]]; then
    printf '\n' >> "${RESULTS_FILE}"
  fi
}

progress_header_printed=0

progress_pattern_heading() {
  local pattern="$1"
  echo "  > Benchmarking current for ${pattern}..."
}

progress_table_header() {
  if [[ "${progress_header_printed}" -eq 1 ]]; then
    return
  fi
  progress_header_printed=1
  echo "      | Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |"
  echo "      |----------|--------------------|----------------|---------------|---------------|---------------|"
}

progress_case_row() {
  local pattern="$1"
  local size="$2"
  local case_log="$3"
  if grep -Eq '^UNSUPPORTED,' "${case_log}" || is_unsupported_output "${case_log}"; then
    printf '      | %-8s | %16s | %12s | %12s | %12s | %12s |\n' "${size}B" "UNSUPPORTED" "UNSUPPORTED" "UNSUPPORTED" "UNSUPPORTED" "UNSUPPORTED"
    return
  fi
  if grep -Eq '^SKIP,' "${case_log}"; then
    printf '      | %-8s | %16s | %12s | %12s | %12s | %12s |\n' "${size}B" "SKIP" "SKIP" "SKIP" "SKIP" "SKIP"
    return
  fi
  python3 - "$pattern" "$size" "$case_log" <<'PY'
import sys

pattern, size, path = sys.argv[1], sys.argv[2], sys.argv[3]
metrics = {}
echo_patterns = {"MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER", "MULTI_STREAM", "MULTI_SPOT_REQREP", "MULTI_SPOT_SENDSEND"}
with open(path, "r", encoding="utf-8", errors="replace") as fh:
    for raw in fh:
        parts = raw.strip().split(",")
        if len(parts) != 7 or parts[0] != "RESULT" or parts[1] != "current":
            continue
        if parts[2] != pattern or parts[4] != size:
            continue
        metrics[parts[5]] = parts[6]

required = ("throughput", "bandwidth", "latency", "latency_p95", "latency_p99")
if not all(key in metrics for key in required):
    print(f"      | {size + 'B':<8} | {'FAIL':>16} | {'FAIL':>12} | {'FAIL':>12} | {'FAIL':>12} | {'FAIL':>12} |")
    raise SystemExit(0)

unit = "Kops/s" if pattern in echo_patterns else "Kmsg/s"
throughput = float(metrics["throughput"]) / 1000.0
bandwidth = float(metrics["bandwidth"])
latency = float(metrics["latency"])
latency_p95 = float(metrics["latency_p95"])
latency_p99 = float(metrics["latency_p99"])
throughput_text = f"{throughput:8.3f} {unit}"
print(
    f"      | {size + 'B':<8} | {throughput_text:>16} | {bandwidth:10.3f} MB/s |"
    f" {latency:9.3f} ms | {latency_p95:9.3f} ms | {latency_p99:9.3f} ms |"
)
PY
}

count_result_lines() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local case_log="$4"
  awk -F',' -v pattern="${pattern}" -v transport="${transport}" -v size="${size}" '
    $1 == "RESULT" && $2 == "current" && $3 == pattern && $4 == transport && $5 == size { count++ }
    END { print count + 0 }
  ' "${case_log}"
}

timeout_seconds_from_ms() {
  local millis="${1:-0}"
  if [[ ! "${millis}" =~ ^[0-9]+$ || "${millis}" -le 0 ]]; then
    echo 0
    return
  fi
  echo $(((millis + 999) / 1000))
}

SERVER_READY_TIMEOUT_SECONDS="$(timeout_seconds_from_ms "${PERF_MULTI_SERVER_READY_TIMEOUT_MS:-10000}")"
SERVER_SHUTDOWN_TIMEOUT_SECONDS="$(timeout_seconds_from_ms "${PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-5000}")"
ONE_WAY_CLIENT_READY_TIMEOUT=10

wait_for_file_prefix() {
  local file_path="$1"
  local prefix="$2"
  local timeout_seconds="$3"
  local deadline=$((SECONDS + timeout_seconds))
  while (( SECONDS < deadline )); do
    if [[ -f "${file_path}" ]]; then
      local line
      line="$(awk -v prefix="${prefix}" 'index($0, prefix) == 1 { print; exit }' "${file_path}" 2>/dev/null || true)"
      if [[ -n "${line}" ]]; then
        printf '%s\n' "${line}"
        return 0
      fi
    fi
    sleep 0.1
  done
  return 1
}

wait_for_ready_or_unsupported() {
  local file_path="$1"
  local timeout_seconds="$2"
  local deadline=$((SECONDS + timeout_seconds))
  while (( SECONDS < deadline )); do
    if [[ -f "${file_path}" ]]; then
      local line
      line="$(awk '/^(READY,|UNSUPPORTED,)/ { print; exit }' "${file_path}" 2>/dev/null || true)"
      if [[ -n "${line}" ]]; then
        printf '%s\n' "${line}"
        return 0
      fi
    fi
    sleep 0.1
  done
  return 1
}

wait_for_pid() {
  local pid="$1"
  local timeout_seconds="$2"
  local deadline=$((SECONDS + timeout_seconds))
  while kill -0 "${pid}" 2>/dev/null; do
    if (( SECONDS >= deadline )); then
      return 1
    fi
    sleep 0.1
  done
  return 0
}

shutdown_server() {
  local pid="$1"
  local control_fd="$2"
  if [[ -n "${control_fd}" ]]; then
    if kill -0 "${pid}" 2>/dev/null; then
      { printf 'STOP\n' >&"${control_fd}"; } 2>/dev/null || true
    fi
    eval "exec ${control_fd}>&-"
  fi
  if wait_for_pid "${pid}" "${SERVER_SHUTDOWN_TIMEOUT_SECONDS}"; then
    wait "${pid}" 2>/dev/null || true
    return
  fi
  kill "${pid}" 2>/dev/null || true
  if wait_for_pid "${pid}" "${SERVER_SHUTDOWN_TIMEOUT_SECONDS}"; then
    wait "${pid}" 2>/dev/null || true
    return
  fi
  kill -9 "${pid}" 2>/dev/null || true
  wait "${pid}" 2>/dev/null || true
}

resolve_client_timeout_seconds() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local duration="$4"
  if [[ -n "${PERF_MULTI_TIMEOUT_SECONDS:-}" ]]; then
    echo "${PERF_MULTI_TIMEOUT_SECONDS}"
    return
  fi
  if [[ "${pattern}" == "MULTI_STREAM" ]]; then
    local stream_timeout=$((duration * 3 + 20))
    (( stream_timeout < 45 )) && stream_timeout=45
    echo "${stream_timeout}"
    return
  fi
  if [[ "${pattern}" == MULTI_SPOT* ]] || { [[ "${transport}" == "tls" || "${transport}" == "wss" ]] && (( size >= 131072 )); }; then
    local spot_timeout=$((duration * 6 + 30))
    (( spot_timeout < 90 )) && spot_timeout=90
    echo "${spot_timeout}"
    return
  fi
  local timeout=$((duration * 3 + 20))
  (( timeout < 45 )) && timeout=45
  echo "${timeout}"
}

run_multi_process_case() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local duration="$4"
  local clients="$5"
  local case_log="$6"

  local srv_out client_out client_err server_fifo ready_line endpoint control_line control_endpoint
  local server_pid server_control_fd client_pid client_control_fd client_fifo client_ready_line
  local client_timeout case_status
  srv_out="$(mktemp "${TMP_DIR}/server.XXXXXX")"
  client_out="$(mktemp "${TMP_DIR}/client.XXXXXX")"
  client_err="$(mktemp "${TMP_DIR}/client_err.XXXXXX")"
  server_fifo="$(mktemp -u "${TMP_DIR}/server_fifo.XXXXXX")"
  mkfifo "${server_fifo}"

  run_go_perf ./perf/multi \
    --role server \
    --pattern "${pattern}" \
    --transport "${transport}" \
    --msg-size "${size}" \
    --duration "${duration}" \
    --clients "${clients}" \
    < "${server_fifo}" > "${srv_out}" 2>&1 &
  server_pid=$!
  exec {server_control_fd}> "${server_fifo}"
  rm -f "${server_fifo}"

  ready_line="$(wait_for_ready_or_unsupported "${srv_out}" "${SERVER_READY_TIMEOUT_SECONDS}" || true)"
  if [[ "${ready_line}" == UNSUPPORTED,* ]]; then
    shutdown_server "${server_pid}" "${server_control_fd}"
    cat "${srv_out}" > "${case_log}"
    rm -f "${srv_out}" "${client_out}" "${client_err}"
    return 1
  fi
  if [[ "${ready_line}" != READY,* ]]; then
    shutdown_server "${server_pid}" "${server_control_fd}"
    cat "${srv_out}" > "${case_log}"
    printf 'FAIL,current,%s,%s,%s,server_ready_timeout\n' "${pattern}" "${transport}" "${size}" >> "${case_log}"
    rm -f "${srv_out}" "${client_out}" "${client_err}"
    return 1
  fi
  endpoint="${ready_line#READY,}"
  endpoint="${endpoint//0.0.0.0/127.0.0.1}"
  control_endpoint=""
  if [[ "${pattern}" == MULTI_SPOT* ]]; then
    control_line="$(wait_for_file_prefix "${srv_out}" "CONTROL_READY," "${SERVER_READY_TIMEOUT_SECONDS}" || true)"
    if [[ "${control_line}" != CONTROL_READY,* ]]; then
      shutdown_server "${server_pid}" "${server_control_fd}"
      cat "${srv_out}" > "${case_log}"
      printf 'FAIL,current,%s,%s,%s,control_ready_timeout\n' "${pattern}" "${transport}" "${size}" >> "${case_log}"
      rm -f "${srv_out}" "${client_out}" "${client_err}"
      return 1
    fi
    control_endpoint="${control_line#CONTROL_READY,}"
    control_endpoint="${control_endpoint//0.0.0.0/127.0.0.1}"
  fi
  client_timeout="$(resolve_client_timeout_seconds "${pattern}" "${transport}" "${size}" "${duration}")"
  case_status=0

  if [[ "${pattern}" == "MULTI_DEALER_DEALER" || "${pattern}" == "MULTI_PUBSUB" ]]; then
    client_fifo="$(mktemp -u "${TMP_DIR}/client_fifo.XXXXXX")"
    mkfifo "${client_fifo}"
    run_go_perf ./perf/multi \
      --role client \
      --pattern "${pattern}" \
      --transport "${transport}" \
      --msg-size "${size}" \
      --duration "${duration}" \
      --clients "${clients}" \
      --endpoint "${endpoint}" \
      < "${client_fifo}" > "${client_out}" 2> "${client_err}" &
    client_pid=$!
    exec {client_control_fd}> "${client_fifo}"
    rm -f "${client_fifo}"

    client_ready_line="$(wait_for_file_prefix "${client_out}" "CLIENT_READY," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
    if [[ "${client_ready_line}" != "CLIENT_READY,${size}" ]]; then
      case_status=1
      kill "${client_pid}" 2>/dev/null || true
      wait "${client_pid}" 2>/dev/null || true
    else
      printf 'START,%s\n' "${size}" >&"${server_control_fd}" || true
      printf 'START,%s\n' "${size}" >&"${client_control_fd}" || true
      if ! wait_for_pid "${client_pid}" "${client_timeout}"; then
        case_status=1
        kill "${client_pid}" 2>/dev/null || true
        wait "${client_pid}" 2>/dev/null || true
      elif ! wait "${client_pid}"; then
        case_status=1
      fi
    fi
    exec {client_control_fd}>&- || true
  elif [[ "${pattern}" == MULTI_SPOT* ]]; then
    client_fifo="$(mktemp -u "${TMP_DIR}/client_fifo.XXXXXX")"
    mkfifo "${client_fifo}"
    run_go_perf ./perf/multi \
      --role client \
      --pattern "${pattern}" \
      --transport "${transport}" \
      --msg-size "${size}" \
      --duration "${duration}" \
      --clients "${clients}" \
      --endpoint "${endpoint},${control_endpoint}" \
      < "${client_fifo}" > "${client_out}" 2> "${client_err}" &
    client_pid=$!
    exec {client_control_fd}> "${client_fifo}"
    rm -f "${client_fifo}"

    client_control_line="$(wait_for_file_prefix "${client_out}" "CLIENT_CONTROL_ENDPOINT," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
    if [[ "${client_control_line}" != CLIENT_CONTROL_ENDPOINT,* ]]; then
      case_status=1
      kill "${client_pid}" 2>/dev/null || true
      wait "${client_pid}" 2>/dev/null || true
    else
      client_control_endpoint="${client_control_line#CLIENT_CONTROL_ENDPOINT,}"
      printf 'CONNECT_CONTROL,%s\n' "${client_control_endpoint}" >&"${server_control_fd}" || true
      control_connected="$(wait_for_file_prefix "${srv_out}" "CONTROL_CONNECTED," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
      if [[ "${control_connected}" != "CONTROL_CONNECTED,${client_control_endpoint}" ]]; then
        case_status=1
        kill "${client_pid}" 2>/dev/null || true
        wait "${client_pid}" 2>/dev/null || true
      else
        printf '%s\n' "${control_connected}" >&"${client_control_fd}" || true
        client_ready_line="$(wait_for_file_prefix "${client_out}" "CLIENT_READY," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
        if [[ "${client_ready_line}" != "CLIENT_READY,${size}" ]]; then
          case_status=1
          kill "${client_pid}" 2>/dev/null || true
          wait "${client_pid}" 2>/dev/null || true
        else
          printf 'START,%s\n' "${size}" >&"${server_control_fd}" || true
          printf 'START,%s\n' "${size}" >&"${client_control_fd}" || true
          if ! wait_for_pid "${client_pid}" "${client_timeout}"; then
            case_status=1
            kill "${client_pid}" 2>/dev/null || true
            wait "${client_pid}" 2>/dev/null || true
          elif ! wait "${client_pid}"; then
            case_status=1
          fi
        fi
      fi
    fi
    exec {client_control_fd}>&- || true
  else
    run_go_perf ./perf/multi \
      --role client \
      --pattern "${pattern}" \
      --transport "${transport}" \
      --msg-size "${size}" \
      --duration "${duration}" \
      --clients "${clients}" \
      --endpoint "${endpoint}" \
      > "${client_out}" 2> "${client_err}" &
    client_pid=$!
    if ! wait_for_pid "${client_pid}" "${client_timeout}"; then
      case_status=1
      kill "${client_pid}" 2>/dev/null || true
      wait "${client_pid}" 2>/dev/null || true
    elif ! wait "${client_pid}"; then
      case_status=1
    fi
  fi

  shutdown_server "${server_pid}" "${server_control_fd}"
  {
    cat "${srv_out}"
    if [[ -s "${client_out}" ]]; then
      cat "${client_out}"
    fi
    if [[ -s "${client_err}" ]]; then
      cat "${client_err}"
    fi
  } > "${case_log}"
  rm -f "${srv_out}" "${client_out}" "${client_err}"
  return "${case_status}"
}

render_tables() {
  python3 - "$TMP_DIR" <<'PY'
from collections import defaultdict
import os
import statistics
import sys

tmp_dir = sys.argv[1]
metrics = ("throughput", "bandwidth", "latency", "latency_p95", "latency_p99")
echo_patterns = {"MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER", "MULTI_STREAM", "MULTI_SPOT_REQREP", "MULTI_SPOT_SENDSEND"}
rows = defaultdict(lambda: defaultdict(list))
for entry in os.listdir(tmp_dir):
    if not entry.endswith(".log"):
        continue
    path = os.path.join(tmp_dir, entry)
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            parts = raw.strip().split(",")
            if len(parts) != 7 or parts[0] != "RESULT" or parts[1] != "current":
                continue
            pattern, transport, size, metric, value = parts[2], parts[3], parts[4], parts[5], parts[6]
            if metric not in metrics:
                continue
            try:
                rows[(pattern, transport, int(size))][metric].append(float(value))
            except ValueError:
                continue

by_pattern = defaultdict(list)
for key, metric_values in rows.items():
    values = {}
    for metric in metrics:
        samples = metric_values.get(metric)
        if not samples:
            break
        values[metric] = statistics.median(samples)
    if len(values) != len(metrics):
        continue
    pattern, transport, size = key
    by_pattern[pattern].append((transport, size, values))

printed_first_pattern = False
for pattern in sorted(by_pattern):
    if printed_first_pattern:
        print()
        print("===============================================================================")
        print()
    printed_first_pattern = True
    direction = "echo" if pattern in echo_patterns else "one-way"
    print(f"## PATTERN: {pattern} ({direction})")
    print()
    transports = defaultdict(list)
    for transport, size, values in by_pattern[pattern]:
        transports[transport].append((size, values))
    for transport in sorted(transports):
        print(f"### Transport: {transport}")
        print("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |")
        print("|----------|--------------------|----------------|---------------|---------------|---------------|")
        for size, values in sorted(transports[transport]):
            if not all(metric in values for metric in metrics):
                continue
            unit = "Kops/s" if pattern in echo_patterns else "Kmsg/s"
            throughput_text = f"{values['throughput'] / 1000.0:8.3f} {unit}"
            print(
                f"| {str(size) + 'B':<8}"
                f" | {throughput_text:>16}"
                f" | {values['bandwidth']:10.3f} MB/s"
                f" | {values['latency']:9.3f} ms"
                f" | {values['latency_p95']:9.3f} ms"
                f" | {values['latency_p99']:9.3f} ms |"
            )
        print()
PY
}

{
  echo "## Effective Options (start)"
  echo "- lang: go"
  echo "- suite: multi"
  echo "- patterns: ${EFFECTIVE_PATTERNS_CSV}"
  echo "- runs: ${RUNS}"
  echo "- transports: ${EFFECTIVE_TRANSPORTS_CSV}"
  echo "- msg_sizes: ${EFFECTIVE_MSG_SIZES_CSV}"
  echo "- clients: ${CLIENTS_DISPLAY}"
  echo "- runtime: ${CORE_LIB}"
  echo "- pin_cpu: ${PIN_CPU}"
  echo "- duration_seconds: ${DURATION}"
  echo
} > "${RESULTS_FILE}"

result_lines=0
unsupported=0
fail=0
expected_cases=0
FAILURES=()
SKIPS=()

for pattern_index in "${!PATTERNS[@]}"; do
  pattern="${PATTERNS[pattern_index]}"
  progress_pattern_heading "${pattern}"
  read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
  size_list="$(pattern_msg_sizes "${pattern}")"
  IFS=',' read -r -a SIZES <<< "${size_list}"
  resolved_clients="${CLIENTS}"
  if [[ -z "${resolved_clients}" ]]; then
    resolved_clients="${PERF_MULTI_CLIENTS:-}"
  fi
  if [[ -z "${resolved_clients}" ]]; then
    if [[ "${pattern}" == "MULTI_STREAM" ]]; then
      resolved_clients="10000"
    else
      resolved_clients="100"
    fi
  fi

  transport_total=0
  for candidate_transport in "${PATTERN_XPORTS[@]}"; do
    if transport_enabled "${candidate_transport}"; then
      transport_total=$((transport_total + 1))
    fi
  done
  transport_seen=0

  for transport in "${PATTERN_XPORTS[@]}"; do
    if ! transport_enabled "${transport}"; then
      continue
    fi
    transport_seen=$((transport_seen + 1))
    echo "    Testing ${transport} | ${size_list}:"
    transport_failures=0
    transport_unsupported=0

    for run in $(seq 1 "${RUNS}"); do
      if [[ "${RUNS}" -gt 1 ]]; then
        echo "      run ${run}/${RUNS}:"
      fi
      progress_header_printed=0

      for size in "${SIZES[@]}"; do
        expected_cases=$((expected_cases + 1))
        case_log="${TMP_DIR}/${pattern}_${transport}_${size}_run${run}.log"
        export PERF_MULTI_MSG_UNIT_BYTES="${size}"
        attempt=1
        case_ok=0
        while true; do
          if run_multi_process_case \
            "${pattern}" \
            "${transport}" \
            "${size}" \
            "${DURATION}" \
            "${resolved_clients}" \
            "${case_log}"; then
            case_ok=1
            break
          fi
          if grep -Eq '^UNSUPPORTED,' "${case_log}" || is_unsupported_output "${case_log}" || [[ "${attempt}" -ge "${CASE_RETRIES}" ]]; then
            break
          fi
          attempt=$((attempt + 1))
          sleep_millis "${RUN_COOLDOWN_MS}"
        done
        if [[ "${case_ok}" -eq 1 ]]; then
          append_case_output "${case_log}"
          case_result_lines="$(count_result_lines "${pattern}" "${transport}" "${size}" "${case_log}")"
          if [[ "${case_result_lines}" -gt 0 ]]; then
            result_lines=$((result_lines + case_result_lines))
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          if grep -Eq '^UNSUPPORTED,' "${case_log}" || is_unsupported_output "${case_log}"; then
            unsupported=$((unsupported + 1))
            expected_cases=$((expected_cases - 1))
            transport_unsupported=1
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            break
          fi
          if grep -Eq '^SKIP,' "${case_log}"; then
            expected_cases=$((expected_cases - 1))
            SKIPS+=("${pattern} current ${transport} ${size}B: skipped")
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          echo "FAIL,current,${pattern},${transport},${size},no_result_lines" >> "${RESULTS_FILE}"
          fail=$((fail + 1))
          transport_failures=$((transport_failures + 1))
          FAILURES+=("${pattern} current ${transport} ${size}B: no_result_lines")
        else
          append_case_output "${case_log}"
          case_result_lines="$(count_result_lines "${pattern}" "${transport}" "${size}" "${case_log}")"
          if [[ "${case_result_lines}" -gt 0 ]]; then
            result_lines=$((result_lines + case_result_lines))
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          if grep -Eq '^UNSUPPORTED,' "${case_log}" || is_unsupported_output "${case_log}"; then
            echo "UNSUPPORTED,current,${pattern},${transport}" >> "${RESULTS_FILE}"
            unsupported=$((unsupported + 1))
            expected_cases=$((expected_cases - 1))
            transport_unsupported=1
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            break
          fi
          if grep -Eq '^SKIP,' "${case_log}"; then
            expected_cases=$((expected_cases - 1))
            SKIPS+=("${pattern} current ${transport} ${size}B: skipped")
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          echo "FAIL,current,${pattern},${transport},${size},exit_nonzero_after_${attempt}_attempts" >> "${RESULTS_FILE}"
          fail=$((fail + 1))
          transport_failures=$((transport_failures + 1))
          FAILURES+=("${pattern} current ${transport} ${size}B: exit_nonzero_after_${attempt}_attempts")
        fi
        progress_table_header
        progress_case_row "${pattern}" "${size}" "${case_log}"
        sleep_millis "${RUN_COOLDOWN_MS}"
      done

      if [[ "${transport_unsupported}" -eq 1 ]]; then
        break
      fi

      if [[ "${RUNS}" -gt 1 && "${run}" -lt "${RUNS}" ]]; then
        echo "      [cooldown ${RUN_COOLDOWN_MS}ms]"
        sleep "$(python3 - <<PY
print(${RUN_COOLDOWN_MS} / 1000.0)
PY
)"
      fi
    done

    if [[ "${transport_unsupported}" -eq 1 ]]; then
      echo "    Testing ${transport}: unsupported Done"
    elif [[ "${transport_failures}" -gt 0 ]]; then
      echo "    Testing ${transport}: (failures=${transport_failures}) Done"
    else
      echo "    Testing ${transport}: Done"
    fi

    if [[ "${transport_seen}" -lt "${transport_total}" ]]; then
      echo "    [transport cooldown ${TRANSPORT_TRANSITION_MS}ms]"
      sleep "$(python3 - <<PY
print(${TRANSPORT_TRANSITION_MS} / 1000.0)
PY
)"
    fi
  done

  if [[ $((pattern_index + 1)) -lt "${#PATTERNS[@]}" ]]; then
    echo "[pattern cooldown ${PATTERN_TRANSITION_MS}ms]"
    sleep "$(python3 - <<PY
print(${PATTERN_TRANSITION_MS} / 1000.0)
PY
)"
  fi
done

table_output="$(render_tables)"
if [[ -n "${table_output}" ]]; then
  printf '\n%s\n' "${table_output}" >> "${RESULTS_FILE}"
fi

if [[ "${#SKIPS[@]}" -gt 0 ]]; then
  {
    echo
    echo "## Skips"
    for skip in "${SKIPS[@]}"; do
      echo "- ${skip}"
    done
  } >> "${RESULTS_FILE}"
fi

if [[ "${#FAILURES[@]}" -gt 0 ]]; then
  {
    echo
    echo "## Failures"
    for failure in "${FAILURES[@]}"; do
      echo "- ${failure}"
    done
  } >> "${RESULTS_FILE}"
fi

expected_result_lines=$((expected_cases * 5))
status="partial"
if [[ "${fail}" -eq 0 && "${result_lines}" -eq "${expected_result_lines}" ]]; then
  status="complete"
fi

{
  echo
  echo "## Effective Options (result)"
  echo "- lang: go"
  echo "- suite: multi"
  echo "- patterns: ${EFFECTIVE_PATTERNS_CSV}"
  echo "- runs: ${RUNS}"
  echo "- transports: ${EFFECTIVE_TRANSPORTS_CSV}"
  echo "- msg_sizes: ${EFFECTIVE_MSG_SIZES_CSV}"
  echo "- clients: ${CLIENTS_DISPLAY}"
  echo "- runtime: ${CORE_LIB}"
  echo "- pin_cpu: ${PIN_CPU}"
  echo "- duration_seconds: ${DURATION}"
  echo "- status: ${status}"
  echo "- expected_result_lines: ${expected_result_lines}"
  echo "- actual_result_lines: ${result_lines}"
} >> "${RESULTS_FILE}"

if [[ -n "${OUTPUT_FILE}" ]]; then
  cp "${RESULTS_FILE}" "${OUTPUT_FILE}"
fi

cat "${RESULTS_FILE}"

if [[ "${status}" != "complete" ]]; then
  exit 1
fi
