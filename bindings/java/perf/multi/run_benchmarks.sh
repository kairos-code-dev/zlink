#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${ROOT_DIR}/../../.." && pwd)"
JAVA_BINDINGS_DIR="$(cd "${ROOT_DIR}/.." && pwd)"
STREAM_CLIENT="${REPO_DIR}/core/build/bin/perf_stream_client"
CORE_BUILD_DIR="${REPO_DIR}/core/build"
RESULTS_ROOT="${ROOT_DIR}/results"
PATTERN="ALL"
if [[ -n "${PERF_TRANSPORTS:-}" ]]; then
  TRANSPORTS="${PERF_TRANSPORTS}"
elif [[ "$(uname -s)" == "Linux" ]]; then
  TRANSPORTS="tcp,ipc"
else
  TRANSPORTS="tcp"
fi
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
CLIENTS="${PERF_MULTI_CLIENTS:-100}"
RUNS=1
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
RESULTS_TAG=""
BUILD_DIR=""
OUTPUT_PATH=""
PIN_CPU=0
REUSE_BUILD=0
CLEAN_BUILD=0
COMMON_IO_THREADS="${PERF_IO_THREADS:-}"
SERVER_IO_THREADS="${PERF_MULTI_SERVER_IO_THREADS:-}"
CLIENT_IO_THREADS="${PERF_MULTI_CLIENT_IO_THREADS:-}"
HWM="${PERF_MULTI_HWM:-${PERF_HWM:-}}"
SEND_HWM="${PERF_MULTI_SNDHWM:-${PERF_SNDHWM:-${HWM}}}"
RECV_HWM="${PERF_MULTI_RCVHWM:-${PERF_RCVHWM:-${HWM}}}"
SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-200}"
RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-200}"
CONNECT_READY_TIMEOUT_MS="${PERF_MULTI_CONNECT_READY_TIMEOUT_MS:-5000}"
TRANSPORT_TRANSITION_MS="${PERF_MULTI_TRANSPORT_TRANSITION_MS:-3000}"
PATTERN_TRANSITION_MS="${PERF_MULTI_PATTERN_TRANSITION_MS:-3000}"
RUN_COOLDOWN_MS="${PERF_MULTI_RUN_COOLDOWN_MS:-3000}"
SERVER_READY_TIMEOUT_MS="${PERF_MULTI_SERVER_READY_TIMEOUT_MS:-10000}"
SERVER_SHUTDOWN_TIMEOUT_MS="${PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-5000}"
SERVER_BIND_PORT="${PERF_MULTI_SERVER_BIND_PORT:-0}"
MONITOR_HWM="${PERF_MULTI_MONITOR_HWM:-1000}"
CONNECT_CONCURRENCY="${PERF_MULTI_CONNECT_CONCURRENCY:-}"
STREAM_DEFAULT_CLIENTS=10000
STREAM_DEFAULT_MSG_SIZES="64,256,1024,65536"
explicit_clients=0
explicit_msg_sizes=0
SKIP_NOFILE_CHECK="${PERF_SKIP_NOFILE_CHECK:-0}"
SKIP_MEMORY_CHECK="${PERF_SKIP_MEMORY_CHECK:-0}"

usage() {
  cat <<'USAGE'
Usage: perf/multi/run_benchmarks.sh [options]

Options:
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --clients N            Client count.
  --runs N               Iterations per pattern/transport/size.
  --duration N           Active duration seconds.
  --build-dir PATH       Build directory override.
  --reuse-build          Reuse existing installDist output.
  --clean-build          Delete build dir before installDist.
  --output PATH          Tee report output to PATH.
  --pin-cpu              Pin benchmark processes to CPU 0 on Linux.
  --io-threads N         Set both server/client io threads.
  --server-io-threads N  Server io threads override.
  --client-io-threads N  Client io threads override.
  --hwm N                Shared HWM fallback.
  --send-hwm N           Send HWM override.
  --recv-hwm N           Receive HWM override.
  --sndtimeo N           Send timeout ms.
  --rcvtimeo N           Receive timeout ms.
  --connect-concurrency N  Client connect concurrency.
  --connect-ready-timeout-ms N  Client connect-ready timeout.
  --transport-transition-ms N   Transport cooldown.
  --pattern-transition-ms N     Pattern cooldown.
  --server-ready-timeout-ms N   Server ready timeout.
  --server-shutdown-timeout-ms N Server shutdown timeout.
  --server-bind-port N    Fixed bind port (0=auto).
  --monitor-hwm N         Monitor socket HWM.
  --results-dir PATH     Results root override.
  --results-tag NAME     Optional report suffix tag.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern) PATTERN="${2:-}"; shift ;;
    --transports) TRANSPORTS="${2:-}"; shift ;;
    --msg-sizes) MSG_SIZES="${2:-}"; explicit_msg_sizes=1; shift ;;
    --clients) CLIENTS="${2:-}"; explicit_clients=1; shift ;;
    --runs) RUNS="${2:-}"; shift ;;
    --duration) DURATION="${2:-}"; shift ;;
    --build-dir) BUILD_DIR="${2:-}"; shift ;;
    --reuse-build) REUSE_BUILD=1 ;;
    --clean-build) CLEAN_BUILD=1 ;;
    --output) OUTPUT_PATH="${2:-}"; shift ;;
    --pin-cpu) PIN_CPU=1 ;;
    --io-threads) COMMON_IO_THREADS="${2:-}"; shift ;;
    --server-io-threads) SERVER_IO_THREADS="${2:-}"; shift ;;
    --client-io-threads) CLIENT_IO_THREADS="${2:-}"; shift ;;
    --hwm) HWM="${2:-}"; SEND_HWM="${2:-}"; RECV_HWM="${2:-}"; shift ;;
    --send-hwm) SEND_HWM="${2:-}"; shift ;;
    --recv-hwm) RECV_HWM="${2:-}"; shift ;;
    --sndtimeo|--send-timeout-ms) SNDTIMEO_MS="${2:-}"; shift ;;
    --rcvtimeo|--recv-timeout-ms) RCVTIMEO_MS="${2:-}"; shift ;;
    --connect-concurrency) CONNECT_CONCURRENCY="${2:-}"; shift ;;
    --connect-ready-timeout-ms) CONNECT_READY_TIMEOUT_MS="${2:-}"; shift ;;
    --transport-transition-ms) TRANSPORT_TRANSITION_MS="${2:-}"; shift ;;
    --pattern-transition-ms) PATTERN_TRANSITION_MS="${2:-}"; shift ;;
    --server-ready-timeout-ms) SERVER_READY_TIMEOUT_MS="${2:-}"; shift ;;
    --server-shutdown-timeout-ms) SERVER_SHUTDOWN_TIMEOUT_MS="${2:-}"; shift ;;
    --server-bind-port) SERVER_BIND_PORT="${2:-}"; shift ;;
    --monitor-hwm) MONITOR_HWM="${2:-}"; shift ;;
    --results-dir) RESULTS_ROOT="${2:-}"; shift ;;
    --results-tag) RESULTS_TAG="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if ! [[ "${RUNS}" =~ ^[0-9]+$ ]] || [[ "${RUNS}" -lt 1 ]]; then
  echo "--runs must be >= 1" >&2
  exit 1
fi

if ! [[ "${CLIENTS}" =~ ^[0-9]+$ ]] || [[ "${CLIENTS}" -lt 1 ]]; then
  echo "--clients must be >= 1" >&2
  exit 1
fi

for numeric_opt in COMMON_IO_THREADS SERVER_IO_THREADS CLIENT_IO_THREADS SEND_HWM RECV_HWM MONITOR_HWM CONNECT_CONCURRENCY; do
  value="${!numeric_opt}"
  if [[ -n "${value}" ]] && { ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 1 ]]; }; then
    echo "${numeric_opt,,} must be >= 1" >&2
    exit 1
  fi
done

for numeric_opt in SNDTIMEO_MS RCVTIMEO_MS CONNECT_READY_TIMEOUT_MS TRANSPORT_TRANSITION_MS PATTERN_TRANSITION_MS RUN_COOLDOWN_MS SERVER_READY_TIMEOUT_MS SERVER_SHUTDOWN_TIMEOUT_MS SERVER_BIND_PORT; do
  value="${!numeric_opt}"
  if [[ -n "${value}" ]] && { ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 0 ]]; }; then
    echo "${numeric_opt,,} must be >= 0" >&2
    exit 1
  fi
done

if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERN="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_STREAM"
fi

detect_platform() {
  case "$(uname -s)" in
    Linux*) echo "linux" ;;
    Darwin*) echo "macos" ;;
    MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
    *) echo "$(uname -s | tr '[:upper:]' '[:lower:]')" ;;
  esac
}

trim_csv() {
  printf '%s' "$1" | awk -F',' '{for (i=1; i<=NF; ++i) {gsub(/^[[:space:]]+|[[:space:]]+$/, "", $i); printf "%s%s", (i>1?",":""), $i}}'
}

default_msg_sizes_for_pattern() {
  local pattern="$1"
  if [[ "${pattern}" == "MULTI_STREAM" ]]; then
    echo "${STREAM_DEFAULT_MSG_SIZES}"
  else
    echo "${MSG_SIZES}"
  fi
}

default_clients_for_pattern() {
  local pattern="$1"
  if [[ "${pattern}" == "MULTI_STREAM" && "${CLIENTS}" == "100" ]]; then
    echo "${STREAM_DEFAULT_CLIENTS}"
  else
    echo "${CLIENTS}"
  fi
}

pick_endpoint() {
  local transport="$1"
  local token="$2"
  if [[ "${transport}" == "ipc" ]]; then
    echo "ipc://${RESULTS_ROOT}/multi/tmp/${token}-${RANDOM}.sock"
  else
    if [[ "${SERVER_BIND_PORT}" != "0" ]]; then
      echo "${transport}://127.0.0.1:${SERVER_BIND_PORT}"
      return
    fi
    local port
    port="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
    echo "${transport}://127.0.0.1:${port}"
  fi
}

wait_for_tcp_endpoint() {
  local endpoint="$1"
  local timeout_ms="${2:-10000}"
  python3 - "$endpoint" "$timeout_ms" <<'PY'
import socket, sys, time
endpoint = sys.argv[1]
timeout_ms = int(sys.argv[2])
host_port = endpoint.split("://", 1)[1]
host, port = host_port.rsplit(":", 1)
port = int(port)
deadline = time.time() + (timeout_ms / 1000.0)
while time.time() < deadline:
    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        if sock.connect_ex((host, port)) == 0:
            raise SystemExit(0)
    finally:
        sock.close()
    time.sleep(0.05)
raise SystemExit(1)
PY
}

sleep_ms() {
  python3 - "$1" <<'PY'
import sys, time
time.sleep(int(sys.argv[1]) / 1000.0)
PY
}

wait_for_pid_or_kill() {
  local pid="$1"
  local timeout_ms="$2"
  local label="$3"
  local deadline=$(( $(date +%s%3N) + timeout_ms ))
  while kill -0 "${pid}" 2>/dev/null; do
    if (( $(date +%s%3N) >= deadline )); then
      kill -TERM "${pid}" 2>/dev/null || true
      sleep_ms 200
      kill -KILL "${pid}" 2>/dev/null || true
      echo "${label} timed out" >&2
      return 1
    fi
    sleep_ms 100
  done
  wait "${pid}"
}

validate_pattern_mode() {
  return 0
}

normalize_multi_pattern() {
  local value="$1"
  value="${value^^}"
  if [[ "${value}" == MULTI_* ]]; then
    printf '%s' "${value}"
  else
    printf 'MULTI_%s' "${value}"
  fi
}

prune_reports() {
  local report_dir="$1"
  local max_files="${PERF_RESULTS_MAX_FILES:-100}"
  if ! is_uint "${max_files}" || [[ "${max_files}" -lt 1 ]]; then
    max_files=100
  fi
  local count
  count="$(find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' | wc -l | tr -d ' ')"
  if [[ -z "${count}" || "${count}" -le "${max_files}" ]]; then
    return
  fi
  find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' -printf '%f\n' \
    | sort \
    | head -n "$((count - max_files))" \
    | while read -r old_file; do
        rm -f "${report_dir}/${old_file}"
      done
}

resolve_build_dir() {
  if [[ -n "${BUILD_DIR}" ]]; then
    printf '%s' "${BUILD_DIR%/}/perf-multi"
  else
    printf '%s' "${ROOT_DIR}/multi/Zlink.BindingBench.Multi/build"
  fi
}

ensure_core_stream_client() {
  if [[ "${REUSE_BUILD}" -eq 1 ]]; then
    if [[ ! -x "${STREAM_CLIENT}" ]]; then
      echo "shared stream client not found for --reuse-build: ${STREAM_CLIENT}" >&2
      exit 1
    fi
    return
  fi

  cmake -S "${REPO_DIR}" -B "${CORE_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LTO=OFF \
    -DBUILD_BENCHMARKS=ON \
    -DZLINK_BUILD_TESTS=OFF \
    -DZLINK_BUILD_BENCH_ZMQ=OFF \
    -DZLINK_BUILD_BENCH_ZLINK=ON \
    -DZLINK_BUILD_BENCH_BEAST=OFF \
    -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF \
    -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF \
    -DZLINK_CXX_STANDARD=17 >/dev/null
  cmake --build "${CORE_BUILD_DIR}" --target perf_stream_client >/dev/null
}

is_uint() {
  local value="${1:-}"
  [[ "${value}" =~ ^[0-9]+$ ]]
}

NOFILE_SKIP_REASON=""
ensure_nofile_limit() {
  local clients="${1:-}"
  NOFILE_SKIP_REASON=""
  if [[ "${SKIP_NOFILE_CHECK}" == "1" ]]; then
    return 0
  fi
  if ! is_uint "${clients}"; then
    return 0
  fi
  local required=$(( clients * 3 + 4096 ))
  local soft hard
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
  if [[ "${SKIP_MEMORY_CHECK}" == "1" ]]; then
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
  local budget_pct="${PERF_MULTI_MEMORY_BUDGET_PCT:-${PERF_MEMORY_BUDGET_PCT:-70}}"
  local base_mb="${PERF_MULTI_MEMORY_BASE_MB:-${PERF_MEMORY_BASE_MB:-512}}"
  local per_client_kb="${PERF_MULTI_MEMORY_PER_CLIENT_KB:-${PERF_MEMORY_PER_CLIENT_KB:-1024}}"
  if ! is_uint "${budget_pct}" || (( budget_pct < 1 || budget_pct > 95 )); then
    echo ""
    return
  fi
  if ! is_uint "${base_mb}" || ! is_uint "${per_client_kb}" || (( per_client_kb < 1 )); then
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
  if [[ "${SKIP_MEMORY_CHECK}" == "1" ]]; then
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
  local available_kb budget_pct base_mb per_client_kb
  available_kb="$(memory_available_kb)"
  budget_pct="${PERF_MULTI_MEMORY_BUDGET_PCT:-${PERF_MEMORY_BUDGET_PCT:-70}}"
  base_mb="${PERF_MULTI_MEMORY_BASE_MB:-${PERF_MEMORY_BASE_MB:-512}}"
  per_client_kb="${PERF_MULTI_MEMORY_PER_CLIENT_KB:-${PERF_MEMORY_PER_CLIENT_KB:-1024}}"
  MEMORY_SKIP_REASON="clients=${clients},max_clients=${max_clients},mem_available_kb=${available_kb},budget_pct=${budget_pct},base_mb=${base_mb},per_client_kb=${per_client_kb}"
  return 1
}

throughput_unit_for_pattern() {
  case "$1" in
    DEALER_ROUTER|ROUTER_ROUTER|STREAM) printf 'Kops/s' ;;
    *) printf 'Kmsg/s' ;;
  esac
}

format_progress_row() {
  local bare_pattern="$1"
  local transport="$2"
  local size="$3"
  local source_file="$4"
  local prefix="$5"
  python3 - "$bare_pattern" "$transport" "$size" "$source_file" "$prefix" <<'PY'
import sys

pattern, transport, size, source_file, prefix = sys.argv[1:]
size = int(size)
unit = "Kops/s" if pattern in {"DEALER_ROUTER", "ROUTER_ROUTER", "STREAM"} else "Kmsg/s"
metrics = {}
with open(source_file, encoding="utf-8") as f:
    for line in f:
        if not line.startswith("RESULT,"):
            continue
        parts = line.strip().split(",")
        if len(parts) != 7:
            continue
        _, _, result_pattern, result_transport, result_size, metric, value = parts
        if result_pattern != pattern or result_transport != transport or int(result_size) != size:
            continue
        metrics[metric] = float(value)
required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
if any(metric not in metrics for metric in required):
    raise SystemExit(1)
print(
    f"{prefix}| {size}B | {metrics['throughput'] / 1000.0:.2f} {unit} | "
    f"{metrics['bandwidth']:.2f} MB/s | {metrics['latency']:.3f} ms | "
    f"{metrics['latency_p95']:.3f} ms | {metrics['latency_p99']:.3f} ms |"
)
PY
}

print_table_header() {
  local prefix="$1"
  echo "${prefix}| Size | Throughput | Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) |"
  echo "${prefix}|------|------------|-----------|--------------|-------------|-------------|"
}

case_status() {
  local public_pattern="$1"
  local transport="$2"
  local size="$3"
  local source_file="$4"
  local prefix="${public_pattern#MULTI_}"
  local unsupported_line
  unsupported_line="$(awk -F',' -v pattern="${prefix}" -v transport="${transport}" \
    '$1=="UNSUPPORTED" && $3==pattern && $4==transport {print $0; exit}' "${source_file}")"
  if [[ -n "${unsupported_line}" ]]; then
    printf 'unsupported,%s\n' "${unsupported_line##*,}"
    return 0
  fi
  local fail_line
  fail_line="$(awk -F',' -v pattern="${prefix}" -v transport="${transport}" -v size="${size}" \
    '$1=="FAIL" && $3==pattern && $4==transport && $5==size {print $0; exit}' "${source_file}")"
  if [[ -n "${fail_line}" ]]; then
    printf 'fail,%s\n' "${fail_line##*,}"
    return 0
  fi
  printf 'ok,-\n'
}

runner_prefix=()
stream_client_prefix=()
if [[ "${PIN_CPU}" -eq 1 ]]; then
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "--pin-cpu is only supported on Linux in this runner" >&2
    exit 1
  fi
  if ! command -v taskset >/dev/null 2>&1; then
    echo "--pin-cpu requires taskset" >&2
    exit 1
  fi
  runner_prefix=("taskset" "-c" "0")
  stream_client_prefix=("taskset" "-c" "0")
fi

mkdir -p "${RESULTS_ROOT}/multi/tmp" "${RESULTS_ROOT}/multi/report"
if [[ -n "${OUTPUT_PATH}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_PATH}")"
  : > "${OUTPUT_PATH}"
  exec > >(tee -a "${OUTPUT_PATH}")
fi
cd "${ROOT_DIR}"
PROJECT_BUILD_DIR="$(resolve_build_dir)"
if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
  rm -rf "${PROJECT_BUILD_DIR}"
fi
if [[ "${REUSE_BUILD}" -eq 0 ]]; then
  "${JAVA_BINDINGS_DIR}/gradlew" --no-daemon -p "${JAVA_BINDINGS_DIR}" \
    -PzlinkPerfBuildDir="${PROJECT_BUILD_DIR}" :perf-multi:installDist >/dev/null
fi
RUNNER="${PROJECT_BUILD_DIR}/install/zlink-java-perf-multi/bin/zlink-java-perf-multi"
if [[ ! -x "${RUNNER}" ]]; then
  if [[ "${REUSE_BUILD}" -eq 1 ]]; then
    echo "runner not found for --reuse-build: ${RUNNER}" >&2
  else
    echo "runner not found: ${RUNNER}" >&2
  fi
  exit 1
fi

platform="$(detect_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report="${RESULTS_ROOT}/multi/report/perf_java_multi_${platform}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

tmp_metrics="$(mktemp)"
tmp_progress="$(mktemp)"
trap 'rm -f "${tmp_metrics}" "${tmp_progress}"' EXIT
metrics_regex='^(throughput|bandwidth|latency|latency_p95|latency_p99)$'

append_metrics() {
  local public_pattern="$1"
  local transport="$2"
  local size="$3"
  local run="$4"
  local source_file="$5"
  local prefix="${public_pattern#MULTI_}"
  local required_count=0

  while IFS= read -r line; do
    [[ "${line}" == RESULT,* ]] || continue
    IFS=',' read -r tag lib result_pattern result_transport result_size metric value <<< "${line}"
    if [[ "${result_pattern}" != "${prefix}" || "${result_transport}" != "${transport}" || "${result_size}" != "${size}" ]]; then
      continue
    fi
    if [[ ! "${metric}" =~ ${metrics_regex} ]]; then
      continue
    fi
    printf '%s,%s,%s,%s,%s,%s\n' \
      "${public_pattern}" "${transport}" "${size}" "${run}" "${metric}" "${value}" >> "${tmp_metrics}"
    case "${metric}" in
      throughput|bandwidth|latency|latency_p95|latency_p99)
        required_count=$((required_count + 1))
        ;;
    esac
  done < "${source_file}"

  if [[ "${required_count}" -ne 5 ]]; then
    echo "missing required RESULT lines for ${public_pattern}/${transport}/${size} run=${run}" >&2
    exit 1
  fi
}

IFS=',' read -r -a patterns <<< "$(trim_csv "${PATTERN}")"
if printf '%s\n' "${patterns[@]}" | grep -qx 'MULTI_STREAM'; then
  ensure_core_stream_client
fi
IFS=',' read -r -a transports <<< "$(trim_csv "${TRANSPORTS}")"
for i in "${!patterns[@]}"; do
  patterns[$i]="$(normalize_multi_pattern "${patterns[$i]}")"
done
requested_patterns="$(IFS=,; echo "${patterns[*]}")"
display_msg_sizes="${MSG_SIZES}"
display_clients="${CLIENTS}"
display_server_io_threads="${SERVER_IO_THREADS:-${COMMON_IO_THREADS:-2}}"
display_client_io_threads="${CLIENT_IO_THREADS:-${COMMON_IO_THREADS:-2}}"
if printf '%s\n' "${patterns[@]}" | grep -qx 'MULTI_STREAM'; then
  if [[ -z "${SERVER_IO_THREADS}" && -z "${COMMON_IO_THREADS}" ]]; then
    display_server_io_threads="${display_server_io_threads} (STREAM: 4)"
  fi
  if [[ -z "${CLIENT_IO_THREADS}" && -z "${COMMON_IO_THREADS}" ]]; then
    display_client_io_threads="${display_client_io_threads} (STREAM: 4)"
  fi
fi

skip_entries=()
run_patterns=()
for pattern in "${patterns[@]}"; do
  bare_pattern="${pattern#MULTI_}"
  validate_pattern_mode "${bare_pattern}"
  pattern_clients="$(default_clients_for_pattern "${pattern}")"
  if ! ensure_nofile_limit "${pattern_clients}"; then
    skip_entries+=("${pattern}: preflight_nofile_${NOFILE_SKIP_REASON}")
    continue
  fi
  if ! ensure_memory_budget "${pattern_clients}"; then
    skip_entries+=("${pattern}: preflight_memory_${MEMORY_SKIP_REASON}")
    continue
  fi
  run_patterns+=("${pattern}")
done

if [[ "${#run_patterns[@]}" -eq 0 ]]; then
  if [[ "${#skip_entries[@]}" -gt 0 ]]; then
    echo
    echo "## Skips"
    for item in "${skip_entries[@]}"; do
      echo "- ${item}"
    done
    exit 0
  fi
  echo "no patterns selected to run" >&2
  exit 1
fi

patterns=("${run_patterns[@]}")

if printf '%s\n' "${patterns[@]}" | grep -qx 'MULTI_STREAM'; then
  if [[ "${explicit_msg_sizes}" -eq 0 ]]; then
    display_msg_sizes="${MSG_SIZES} (STREAM: ${STREAM_DEFAULT_MSG_SIZES})"
  fi
  if [[ "${explicit_clients}" -eq 0 && "${CLIENTS}" == "100" ]]; then
    display_clients="${CLIENTS} (STREAM: ${STREAM_DEFAULT_CLIENTS})"
  fi
fi

echo "  > Benchmarking current for $(IFS=,; echo "${patterns[*]}")..."
printf '  > Benchmarking current for %s...\n' "$(IFS=,; echo "${patterns[*]}")" >> "${tmp_progress}"
for pattern_index in "${!patterns[@]}"; do
  pattern="${patterns[pattern_index]}"
  bare_pattern="${pattern#MULTI_}"
  pattern_clients="$(default_clients_for_pattern "${pattern}")"
  pattern_msg_sizes="$(default_msg_sizes_for_pattern "${pattern}")"
  pattern_server_io_threads="${SERVER_IO_THREADS:-${COMMON_IO_THREADS:-2}}"
  pattern_client_io_threads="${CLIENT_IO_THREADS:-${COMMON_IO_THREADS:-2}}"
  if [[ "${pattern}" == "MULTI_STREAM" ]]; then
    pattern_server_io_threads="${SERVER_IO_THREADS:-${COMMON_IO_THREADS:-4}}"
    pattern_client_io_threads="${CLIENT_IO_THREADS:-${COMMON_IO_THREADS:-4}}"
  fi
  IFS=',' read -r -a msg_sizes <<< "$(trim_csv "${pattern_msg_sizes}")"

  for transport_index in "${!transports[@]}"; do
    transport="${transports[transport_index]}"
    echo "    Testing ${transport} | ${pattern_msg_sizes}:"
    printf '    Testing %s | %s:\n' "${transport}" "${pattern_msg_sizes}" >> "${tmp_progress}"
    if (( RUNS == 1 )); then
      print_table_header "      "
      print_table_header "      " >> "${tmp_progress}"
    fi
    transport_failures=0
    transport_unsupported=0
    for size in "${msg_sizes[@]}"; do
      for ((run=1; run<=RUNS; run++)); do
        if (( RUNS > 1 )); then
          if (( run == 1 )) || (( size == msg_sizes[0] )); then
            :
          fi
        fi
        server_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_server.log"
        client_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_client.log"
        rm -f "${server_log}" "${client_log}"

        if [[ "${bare_pattern}" == "STREAM" ]]; then
          fifo="${RESULTS_ROOT}/multi/tmp/stream_control_${transport}_${size}.fifo"
          rm -f "${fifo}"
          mkfifo "${fifo}"
          endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
          exec 3<>"${fifo}"
          stream_server_cmd=("${runner_prefix[@]}" "${RUNNER}" --multi-server "${pattern}" "${transport}" "${size}" \
            --endpoint "${endpoint}" --clients "${pattern_clients}" \
            --duration "${DURATION}" --control-port 0 \
            --io-threads "${pattern_server_io_threads}" \
            --sndtimeo "${SNDTIMEO_MS}" --rcvtimeo "${RCVTIMEO_MS}" \
            --monitor-hwm "${MONITOR_HWM}" --connect-ready-timeout-ms "${CONNECT_READY_TIMEOUT_MS}" \
            --connect-concurrency "${CONNECT_CONCURRENCY:-$( [[ "${pattern_clients}" -ge 10000 ]] && echo 1024 || echo 128 )}")
          if [[ -n "${SEND_HWM}" ]]; then
            stream_server_cmd+=(--send-hwm "${SEND_HWM}")
          fi
          if [[ -n "${RECV_HWM}" ]]; then
            stream_server_cmd+=(--recv-hwm "${RECV_HWM}")
          fi
          "${stream_server_cmd[@]}" <"${fifo}" >"${server_log}" 2>&1 &
          server_pid=$!
          wait_for_tcp_endpoint "${endpoint}" "${SERVER_READY_TIMEOUT_MS}"
          "${stream_client_prefix[@]}" "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM \
            --sizes "${size}" --runs 1 --duration "${DURATION}" \
            --ccu "${pattern_clients}" --send-stop-token 1 --endpoint "${endpoint}" \
            >"${client_log}" 2>&1
          printf 'STOP\n' >&3
          exec 3>&-
          wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "stream server"
          rm -f "${fifo}"
          status_record="$(case_status "${pattern}" "${transport}" "${size}" "${client_log}")"
          case "${status_record%%,*}" in
            unsupported)
              transport_unsupported=1
              break
              ;;
            fail)
              transport_failures=$((transport_failures + 1))
              continue
              ;;
          esac
          append_metrics "${pattern}" "${transport}" "${size}" "${run}" "${client_log}"
          row="$(format_progress_row "${bare_pattern}" "${transport}" "${size}" "${client_log}" "      ")"
          echo "${row}"
          echo "${row}" >> "${tmp_progress}"
          if (( run < RUNS )); then
            echo "[cooldown ${RUN_COOLDOWN_MS}ms]"
            sleep_ms "${RUN_COOLDOWN_MS}"
          fi
          continue
        fi

        endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
        control_port="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
        server_cmd=("${runner_prefix[@]}" "${RUNNER}" --multi-server "${pattern}" "${transport}" "${size}" \
          --endpoint "${endpoint}" --clients "${pattern_clients}" \
          --duration "${DURATION}" --control-port "${control_port}" \
          --io-threads "${pattern_server_io_threads}" \
          --sndtimeo "${SNDTIMEO_MS}" --rcvtimeo "${RCVTIMEO_MS}" \
          --monitor-hwm "${MONITOR_HWM}" --connect-ready-timeout-ms "${CONNECT_READY_TIMEOUT_MS}")
        if [[ -n "${SEND_HWM}" ]]; then
          server_cmd+=(--send-hwm "${SEND_HWM}")
        fi
        if [[ -n "${RECV_HWM}" ]]; then
          server_cmd+=(--recv-hwm "${RECV_HWM}")
        fi
        server_cmd+=(--connect-concurrency "${CONNECT_CONCURRENCY:-$( [[ "${pattern_clients}" -ge 10000 ]] && echo 1024 || echo 128 )}")
        "${server_cmd[@]}" >"${server_log}" 2>&1 &
        server_pid=$!
        client_cmd=("${runner_prefix[@]}" "${RUNNER}" --multi-client "${pattern}" "${transport}" "${size}" \
          --endpoint "${endpoint}" --clients "${pattern_clients}" \
          --duration "${DURATION}" --control-port "${control_port}" \
          --io-threads "${pattern_client_io_threads}" \
          --sndtimeo "${SNDTIMEO_MS}" --rcvtimeo "${RCVTIMEO_MS}" \
          --monitor-hwm "${MONITOR_HWM}" --connect-ready-timeout-ms "${CONNECT_READY_TIMEOUT_MS}")
        if [[ -n "${SEND_HWM}" ]]; then
          client_cmd+=(--send-hwm "${SEND_HWM}")
        fi
        if [[ -n "${RECV_HWM}" ]]; then
          client_cmd+=(--recv-hwm "${RECV_HWM}")
        fi
        client_cmd+=(--connect-concurrency "${CONNECT_CONCURRENCY:-$( [[ "${pattern_clients}" -ge 10000 ]] && echo 1024 || echo 128 )}")
        "${client_cmd[@]}" >"${client_log}" 2>&1
        wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server"
        metric_log="${server_log}"
        if [[ "${bare_pattern}" == "DEALER_ROUTER" || "${bare_pattern}" == "ROUTER_ROUTER" \
           || "${bare_pattern}" == "PUBSUB" || "${bare_pattern}" == "SPOT" \
           || "${bare_pattern}" == "STREAM" ]]; then
          metric_log="${client_log}"
        fi
        status_record="$(case_status "${pattern}" "${transport}" "${size}" "${metric_log}")"
        case "${status_record%%,*}" in
          unsupported)
            transport_unsupported=1
            break
            ;;
          fail)
            transport_failures=$((transport_failures + 1))
            continue
            ;;
        esac
        append_metrics "${pattern}" "${transport}" "${size}" "${run}" "${metric_log}"
        row="$(format_progress_row "${bare_pattern}" "${transport}" "${size}" "${metric_log}" "      ")"
        echo "${row}"
        echo "${row}" >> "${tmp_progress}"
        if (( run < RUNS )); then
          echo "[cooldown ${RUN_COOLDOWN_MS}ms]"
          sleep_ms "${RUN_COOLDOWN_MS}"
        fi
      done
      if (( transport_unsupported == 1 )); then
        break
      fi
    done
    if (( transport_unsupported == 1 )); then
      echo "    Testing ${transport}: unsupported Done"
      printf '    Testing %s: unsupported Done\n' "${transport}" >> "${tmp_progress}"
    elif (( transport_failures > 0 )); then
      echo "    Testing ${transport}: (failures=${transport_failures}) Done"
      printf '    Testing %s: (failures=%s) Done\n' "${transport}" "${transport_failures}" >> "${tmp_progress}"
    else
      echo "    Testing ${transport}: Done"
      printf '    Testing %s: Done\n' "${transport}" >> "${tmp_progress}"
    fi
    if (( transport_index + 1 < ${#transports[@]} )); then
      echo "    [transport cooldown ${TRANSPORT_TRANSITION_MS}ms]"
      printf '    [transport cooldown %sms]\n' "${TRANSPORT_TRANSITION_MS}" >> "${tmp_progress}"
      sleep_ms "${TRANSPORT_TRANSITION_MS}"
    fi
  done
  if (( pattern_index + 1 < ${#patterns[@]} )); then
    echo "[pattern cooldown ${PATTERN_TRANSITION_MS}ms]"
    printf '[pattern cooldown %sms]\n' "${PATTERN_TRANSITION_MS}" >> "${tmp_progress}"
    sleep_ms "${PATTERN_TRANSITION_MS}"
  fi
done

python3 - "${tmp_metrics}" "${report}" "${requested_patterns}" "${TRANSPORTS}" "${display_msg_sizes}" \
  "${display_clients}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" \
  "${PIN_CPU}" "${display_server_io_threads}" "${display_client_io_threads}" \
  "${HWM}" "${SEND_HWM}" "${RECV_HWM}" "${SNDTIMEO_MS}" "${RCVTIMEO_MS}" \
  "${CONNECT_READY_TIMEOUT_MS}" "${MONITOR_HWM}" "${SERVER_BIND_PORT}" \
  "${CONNECT_CONCURRENCY}" "${tmp_progress}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, clients, runs, duration, results_tag, pin_cpu, server_io_threads, client_io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, connect_ready_timeout_ms, monitor_hwm, server_bind_port, connect_concurrency, progress_path = sys.argv[1:]
runs = int(runs)
required_metrics = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
all_metrics = required_metrics

rows = defaultdict(lambda: defaultdict(list))
patterns = []
pattern_transports = defaultdict(list)
pattern_sizes = defaultdict(list)
pattern_clients = {}

with open(metrics_path, newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    for pattern, transport, size, run, metric, value in reader:
        key = (pattern, transport, int(size))
        if pattern not in patterns:
            patterns.append(pattern)
        if transport not in pattern_transports[pattern]:
            pattern_transports[pattern].append(transport)
        if int(size) not in pattern_sizes[pattern]:
            pattern_sizes[pattern].append(int(size))
        try:
            rows[key][metric].append(float(value))
        except ValueError:
            rows[key][metric].append(math.nan)

for pattern in pattern_sizes:
    pattern_sizes[pattern].sort()

def median(values):
    usable = [v for v in values if not math.isnan(v)]
    if not usable:
        return math.nan
    usable.sort()
    mid = len(usable) // 2
    if len(usable) % 2 == 1:
        return usable[mid]
    return (usable[mid - 1] + usable[mid]) / 2.0

def fmt_metric(value):
    return "N/A" if math.isnan(value) else f"{value:.3f}"

def fmt_rate(value):
    if math.isnan(value):
        return "N/A"
    return f"{value / 1000.0:.2f}"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} MB/s"

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value / 1000.0:.2f} ms"

def fmt_size(size):
    return f"{size}B"

expected = 0
actual = 0
lines = []

def emit(line=""):
    lines.append(line)

def emit_effective_options(section):
    emit(f"## Effective Options ({section})")
    emit("- lang: java")
    emit("- suite: multi")
    emit(f"- runs: {runs}")
    emit(f"- patterns: {pattern_csv}")
    emit(f"- transports: {transports_csv}")
    emit(f"- msg_sizes: {msg_sizes_csv}")
    emit(f"- clients: {clients}")
    emit(f"- pin_cpu: {'on' if pin_cpu == '1' else 'off'}")
    emit(f"- server_io_threads: {server_io_threads}")
    emit(f"- client_io_threads: {client_io_threads}")
    emit(f"- hwm: {hwm or 'default(binding)'}")
    emit(f"- send_hwm: {send_hwm or 'default(binding)'}")
    emit(f"- recv_hwm: {recv_hwm or 'default(binding)'}")
    emit(f"- send_timeout_ms: {sndtimeo_ms}")
    emit(f"- recv_timeout_ms: {rcvtimeo_ms}")
    emit(f"- connect_concurrency: {connect_concurrency or 'auto'}")
    emit(f"- connect_ready_timeout_ms: {connect_ready_timeout_ms}")
    emit(f"- monitor_hwm: {monitor_hwm}")
    emit(f"- server_bind_port: {server_bind_port}")
    emit(f"- duration_seconds: {duration}")
    if results_tag:
        emit(f"- results_tag: {results_tag}")
    emit("")

emit_effective_options("start")
emit("===============================================================================")
emit("")
with open(progress_path, encoding="utf-8") as progress_file:
    progress_text = progress_file.read().strip()
if progress_text:
    emit(progress_text)
    emit("")

result_lines = []
for pattern in patterns:
    emit(f"## PATTERN: {pattern}")
    emit("")
    for transport in pattern_transports[pattern]:
        emit(f"### Transport: {transport}")
        emit("| Size | Throughput | Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) |")
        emit("|------|------------|-----------|--------------|-------------|-------------|")
        rate_unit = "Kops/s" if pattern in {"MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER", "MULTI_STREAM"} else "Kmsg/s"
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in all_metrics}
            expected += 5
            if all(rows[key].get(metric) for metric in required_metrics):
                actual += 5
            emit(
                f"| {fmt_size(size)} | {fmt_rate(metric_values['throughput'])} {rate_unit} | "
                f"{fmt_bandwidth(metric_values['bandwidth'])} | "
                f"{fmt_latency_ms(metric_values['latency'])} | "
                f"{fmt_latency_ms(metric_values['latency_p95'])} | "
                f"{fmt_latency_ms(metric_values['latency_p99'])} |"
            )
            for metric in all_metrics:
                if rows[key].get(metric):
                    result_lines.append(
                        f"RESULT,current,{pattern},{transport},{size},{metric},{fmt_metric(metric_values[metric])}"
                    )
        emit("")

for line in result_lines:
    emit(line)

emit("")
emit_effective_options("result")
emit("## Completion")
emit(f"- status: {'complete' if expected == actual else 'partial'}")
emit(f"- expected_result_lines: {expected}")
emit(f"- actual_result_lines: {actual}")
emit("")
emit("## Failures")

text = "\n".join(lines) + "\n"
with open(report_path, "w", encoding="utf-8") as report_file:
    report_file.write(text)
sys.stdout.write(text)
PY

prune_reports "${RESULTS_ROOT}/multi/report"
echo "saved report: ${report}"
