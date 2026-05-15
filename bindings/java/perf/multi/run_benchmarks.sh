#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${ROOT_DIR}/../../.." && pwd)"
JAVA_BINDINGS_DIR="$(cd "${ROOT_DIR}/.." && pwd)"
STREAM_CLIENT="${REPO_DIR}/bindings/c/build/perf/perf_stream_client"
CORE_BUILD_DIR="${REPO_DIR}/bindings/c/build"
VERSION_FILE="${REPO_DIR}/VERSION"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_RUNTIME="${REPO_DIR}/core/build/lib/libzlink.so.${CORE_VERSION}"
RESULTS_ROOT="${PERF_RESULTS_DIR:-${ROOT_DIR}/results}"
PATTERN="ALL"
if [[ -n "${PERF_TRANSPORTS:-}" ]]; then
  TRANSPORTS="${PERF_TRANSPORTS}"
else
  TRANSPORTS="tcp,tls,ws,wss"
fi
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
CLIENTS="${PERF_MULTI_CLIENTS:-${PERF_CLIENTS:-100}}"
RUNS=1
DURATION="${PERF_MULTI_DURATION_SECONDS:-${PERF_DURATION_SECONDS:-5}}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"
BUILD_DIR=""
OUTPUT_PATH=""
PIN_CPU=0
REUSE_BUILD=0
CLEAN_BUILD=0
COMMON_IO_THREADS="${PERF_IO_THREADS:-4}"
SERVER_IO_THREADS="${PERF_MULTI_SERVER_IO_THREADS:-}"
CLIENT_IO_THREADS="${PERF_MULTI_CLIENT_IO_THREADS:-}"
HWM="${PERF_MULTI_HWM:-${PERF_HWM:-}}"
SEND_HWM="${PERF_MULTI_SNDHWM:-${PERF_SNDHWM:-${HWM}}}"
RECV_HWM="${PERF_MULTI_RCVHWM:-${PERF_RCVHWM:-${HWM}}}"
SNDBUF="${PERF_MULTI_SNDBUF:-${PERF_SNDBUF:-}}"
RCVBUF="${PERF_MULTI_RCVBUF:-${PERF_RCVBUF:-}}"
SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-${PERF_SNDTIMEO_MS:-200}}"
RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-${PERF_RCVTIMEO_MS:-200}}"
CTX_AUTO_HWM_ENABLE="${PERF_CTX_AUTO_HWM_ENABLE:-1}"
CTX_AUTO_HWM_PROFILE="${PERF_MULTI_CTX_AUTO_HWM_PROFILE:-${PERF_CTX_AUTO_HWM_PROFILE:-balanced}}"
CONNECT_READY_TIMEOUT_MS="${PERF_MULTI_CONNECT_READY_TIMEOUT_MS:-${PERF_CONNECT_READY_TIMEOUT_MS:-5000}}"
TRANSPORT_TRANSITION_MS="${PERF_MULTI_TRANSPORT_TRANSITION_MS:-${PERF_TRANSPORT_TRANSITION_MS:-3000}}"
PATTERN_TRANSITION_MS="${PERF_MULTI_PATTERN_TRANSITION_MS:-${PERF_PATTERN_TRANSITION_MS:-3000}}"
RUN_COOLDOWN_MS="${PERF_MULTI_RUN_COOLDOWN_MS:-${PERF_RUN_COOLDOWN_MS:-3000}}"
SERVER_READY_TIMEOUT_MS="${PERF_MULTI_SERVER_READY_TIMEOUT_MS:-${PERF_SERVER_READY_TIMEOUT_MS:-10000}}"
SERVER_SHUTDOWN_TIMEOUT_MS="${PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-${PERF_SERVER_SHUTDOWN_TIMEOUT_MS:-5000}}"
SERVER_BIND_PORT="${PERF_MULTI_SERVER_BIND_PORT:-${PERF_SERVER_BIND_PORT:-0}}"
MONITOR_HWM="${PERF_MULTI_MONITOR_HWM:-${PERF_MONITOR_HWM:-1000}}"
CONNECT_CONCURRENCY="${PERF_MULTI_CONNECT_CONCURRENCY:-${PERF_CONNECT_CONCURRENCY:-}}"
STREAM_DEFAULT_CLIENTS=10000
STREAM_DEFAULT_MSG_SIZES="64,256,1024,65536"
explicit_clients=0
explicit_msg_sizes=0
SKIP_NOFILE_CHECK="${PERF_SKIP_NOFILE_CHECK:-0}"
SKIP_MEMORY_CHECK="${PERF_SKIP_MEMORY_CHECK:-0}"
SPOT_CLEAN_LATENCY="${PERF_MULTI_SPOT_CLEAN_LATENCY:-1}"
DISABLE_RESOURCE_METRICS="${PERF_DISABLE_RESOURCE_METRICS:-0}"
TIMEOUT_SECONDS="${PERF_MULTI_TIMEOUT_SECONDS:-${PERF_TIMEOUT_SECONDS:-auto}}"
SERVICE_CLIENTS="${PERF_MULTI_SERVICE_CLIENTS:-${PERF_SERVICE_CLIENTS:-auto}}"
LAT_TIMEOUT_MS="${PERF_MULTI_LAT_TIMEOUT_MS:-5000}"
STREAM_NON_TCP_CLIENTS_MAX="${PERF_MULTI_STREAM_NON_TCP_CLIENTS_MAX:-10000}"

usage() {
  cat <<'USAGE'
Usage: perf/multi/run_benchmarks.sh [options]

Options:
  -h, --help            Show this help.
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --clients N            Client count.
  --runs N               Iterations per pattern/transport/size.
  --duration N           Active duration seconds.
  --run-cooldown-ms N    Cooldown between repeated runs.
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
  --buf SIZE             Send/receive buffer override.
  --sndbuf SIZE          Send buffer override.
  --rcvbuf SIZE          Receive buffer override.
  --sndtimeo N           Send timeout ms.
  --rcvtimeo N           Receive timeout ms.
  --send-timeout-ms N    Alias of --sndtimeo.
  --recv-timeout-ms N    Alias of --rcvtimeo.
  --connect-concurrency N  Client connect concurrency.
  --connect-ready-timeout-ms N  Client connect-ready timeout.
  --transport-transition-ms N   Transport cooldown.
  --pattern-transition-ms N     Pattern cooldown.
  --server-ready-timeout-ms N   Server ready timeout.
  --server-shutdown-timeout-ms N Server shutdown timeout.
  --server-bind-port N    Fixed bind port (0=auto).
  --monitor-hwm N         Monitor socket HWM.
  --auto-hwm-profile NAME Auto-HWM profile.
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
    --run-cooldown-ms) RUN_COOLDOWN_MS="${2:-}"; shift ;;
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
    --buf) SNDBUF="${2:-}"; RCVBUF="${2:-}"; shift ;;
    --sndbuf) SNDBUF="${2:-}"; shift ;;
    --rcvbuf) RCVBUF="${2:-}"; shift ;;
    --auto-hwm-profile) CTX_AUTO_HWM_PROFILE="${2:-}"; shift ;;
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

if [[ -n "${HWM}${SEND_HWM}${RECV_HWM}${SNDBUF}${RCVBUF}" \
  && "${PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES:-${PERF_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}}" != "1" ]]; then
  echo "manual HWM/SNDBUF/RCVBUF overrides are debug-only; set PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1" >&2
  exit 1
fi

case "${CTX_AUTO_HWM_PROFILE}" in
  ""|compact|low_latency|low-latency|balanced|throughput) ;;
  *)
    echo "--auto-hwm-profile must be compact, low_latency, balanced, or throughput" >&2
    exit 1
    ;;
esac

case "${CTX_AUTO_HWM_ENABLE}" in
  0|1) ;;
  *)
    echo "PERF_CTX_AUTO_HWM_ENABLE must be 0 or 1" >&2
    exit 1
    ;;
esac

export PERF_CTX_AUTO_HWM_ENABLE="${CTX_AUTO_HWM_ENABLE}"
export PERF_CTX_AUTO_HWM_PROFILE="${CTX_AUTO_HWM_PROFILE}"
if [[ "${PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES:-${PERF_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}}" == "1" ]]; then
  export PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1
fi

for numeric_opt in SNDTIMEO_MS RCVTIMEO_MS CONNECT_READY_TIMEOUT_MS TRANSPORT_TRANSITION_MS PATTERN_TRANSITION_MS RUN_COOLDOWN_MS SERVER_READY_TIMEOUT_MS SERVER_SHUTDOWN_TIMEOUT_MS SERVER_BIND_PORT; do
  value="${!numeric_opt}"
  if [[ -n "${value}" ]] && { ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 0 ]]; }; then
    echo "${numeric_opt,,} must be >= 0" >&2
    exit 1
  fi
done

if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERN="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND,MULTI_STREAM"
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
  if [[ "${pattern}" == "MULTI_STREAM" && "${explicit_msg_sizes}" -eq 0 ]]; then
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
    echo "${transport}://127.0.0.1:$(pick_port)"
  fi
}

pick_port() {
  local port
  port="$(python3 - <<'PY'
import socket
try:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
    s.close()
except OSError:
    print("")
PY
)"
  if [[ "${port}" =~ ^[0-9]+$ ]] && [[ "${port}" -gt 0 ]]; then
    printf '%s\n' "${port}"
    return
  fi
  printf '%s\n' "$((20000 + (RANDOM % 20000)))"
}

sleep_ms() {
  python3 - "$1" <<'PY'
import sys, time
time.sleep(int(sys.argv[1]) / 1000.0)
PY
}

wait_for_log_token() {
  local file="$1"
  local token="$2"
  local timeout_ms="$3"
  python3 - "$file" "$token" "$timeout_ms" <<'PY'
import os
import sys
import time

path, token, timeout_ms = sys.argv[1], sys.argv[2], int(sys.argv[3])
deadline = time.time() + (timeout_ms / 1000.0)
position = 0
while time.time() < deadline:
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as handle:
            handle.seek(position)
            for raw in handle:
                line = raw.rstrip("\n")
                if line.startswith(token):
                    print(line)
                    raise SystemExit(0)
            position = handle.tell()
    time.sleep(0.05)
raise SystemExit(1)
PY
}

is_start_gated_pattern() {
  case "$1" in
    DEALER_DEALER|PUBSUB|SPOT|SPOT_REQREP|SPOT_SENDSEND) return 0 ;;
    *) return 1 ;;
  esac
}

is_spot_control_pattern() {
  case "$1" in
    SPOT|SPOT_REQREP|SPOT_SENDSEND) return 0 ;;
    *) return 1 ;;
  esac
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
      wait "${pid}" 2>/dev/null || true
      echo "${label} timed out" >&2
      return 1
    fi
    sleep_ms 100
  done
  wait "${pid}" 2>/dev/null
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

ensure_multi_runner() {
  local build_dir="$1"
  local runner_path="$2"
  local install_dir="${build_dir}/install"
  local dist_zip="${build_dir}/distributions/zlink-java-perf-multi.zip"
  if [[ -x "${runner_path}" && -s "${runner_path}" ]]; then
    return 0
  fi
  rm -f "${runner_path}"
  if [[ -f "${dist_zip}" ]]; then
    mkdir -p "${install_dir}"
    unzip -qo "${dist_zip}" -d "${install_dir}"
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

  cmake -S "${REPO_DIR}/bindings/c" -B "${CORE_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LTO=OFF \
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
  local budget_pct="${PERF_MULTI_MEMORY_BUDGET_PCT:-70}"
  local base_mb="${PERF_MULTI_MEMORY_BASE_MB:-512}"
  local per_client_kb="${PERF_MULTI_MEMORY_PER_CLIENT_KB:-1024}"
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
  budget_pct="${PERF_MULTI_MEMORY_BUDGET_PCT:-70}"
  base_mb="${PERF_MULTI_MEMORY_BASE_MB:-512}"
  per_client_kb="${PERF_MULTI_MEMORY_PER_CLIENT_KB:-1024}"
  MEMORY_SKIP_REASON="clients=${clients},max_clients=${max_clients},mem_available_kb=${available_kb},budget_pct=${budget_pct},base_mb=${base_mb},per_client_kb=${per_client_kb}"
  return 1
}

throughput_unit_for_pattern() {
  case "$1" in
    DEALER_ROUTER|ROUTER_ROUTER|SPOT_REQREP|SPOT_SENDSEND|STREAM) printf 'Kops/s' ;;
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
unit = "Kops/s" if pattern in {"DEALER_ROUTER", "ROUTER_ROUTER", "SPOT_REQREP", "SPOT_SENDSEND", "STREAM"} else "Kmsg/s"
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

format_median_progress_row() {
  local public_pattern="$1"
  local transport="$2"
  local size="$3"
  local metrics_file="$4"
  local prefix="$5"
  python3 - "$public_pattern" "$transport" "$size" "$metrics_file" "$prefix" <<'PY'
import csv
import math
import sys
from collections import defaultdict

pattern, transport, size, metrics_file, prefix = sys.argv[1:]
size = int(size)
bare = pattern.removeprefix("MULTI_")
unit = "Kops/s" if pattern in {"MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER", "MULTI_SPOT_REQREP", "MULTI_SPOT_SENDSEND", "MULTI_STREAM"} else "Kmsg/s"
values = defaultdict(list)
with open(metrics_file, newline="", encoding="utf-8") as f:
    for row in csv.reader(f):
        if len(row) != 6:
            continue
        p, t, s, _run, metric, value = row
        if p == pattern and t == transport and int(s) == size:
            values[metric].append(float(value))

def median(items):
    usable = [v for v in items if not math.isnan(v)]
    if not usable:
        raise SystemExit(1)
    usable.sort()
    mid = len(usable) // 2
    return usable[mid] if len(usable) % 2 else (usable[mid - 1] + usable[mid]) / 2.0

required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
metrics = {metric: median(values[metric]) for metric in required}
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
    printf 'unsupported,-\n'
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
RUNNER="${PROJECT_BUILD_DIR}/install/zlink-java-perf-multi/bin/zlink-java-perf-multi"
if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
  rm -rf "${PROJECT_BUILD_DIR}"
fi
ensure_multi_runner "${PROJECT_BUILD_DIR}" "${RUNNER}"
if [[ "${REUSE_BUILD}" -eq 0 ]]; then
  "${JAVA_BINDINGS_DIR}/gradlew" --no-daemon -p "${JAVA_BINDINGS_DIR}" \
    -PzlinkPerfBuildDir="${PROJECT_BUILD_DIR}" :perf-multi:installDist >/dev/null
fi
ensure_multi_runner "${PROJECT_BUILD_DIR}" "${RUNNER}"
if [[ ! -f "${CORE_RUNTIME}" ]]; then
  echo "core runtime not found: ${CORE_RUNTIME}" >&2
  echo "Build core/build before running Java perf." >&2
  exit 1
fi
if find "${REPO_DIR}/core/include" "${REPO_DIR}/core/src" \
    -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
    -newer "${CORE_RUNTIME}" -print -quit | grep -q .; then
  echo "core runtime is older than core source: ${CORE_RUNTIME}" >&2
  echo "Run: cmake --build core/build" >&2
  exit 1
fi
export ZLINK_LIBRARY_PATH="${CORE_RUNTIME}"
echo "Perf runtime libzlink: ${CORE_RUNTIME}"
if [[ ! -x "${RUNNER}" || ! -s "${RUNNER}" ]]; then
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
tmp_failures="$(mktemp)"
tmp_auto_hwm="$(mktemp)"
trap 'rm -f "${tmp_metrics}" "${tmp_progress}" "${tmp_failures}" "${tmp_auto_hwm}"' EXIT
metrics_regex='^(throughput|bandwidth|latency|latency_p95|latency_p99)$'

expected_result_lines=0
actual_result_lines=0

record_failure() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local run="$4"
  local reason="$5"
  printf '%s,%s,%s,%s,%s\n' \
    "${pattern}" "${transport}" "${size}" "${run}" "${reason}" >> "${tmp_failures}"
}

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
    return 1
  fi

  actual_result_lines=$((actual_result_lines + required_count))
}

append_auto_hwm_details() {
  local source_file="$1"
  [[ -f "${source_file}" ]] || return 0
  awk '/^AUTO_HWM_DETAIL,/ {print}' "${source_file}" >> "${tmp_auto_hwm}" || true
}

resolve_case_connect_concurrency() {
  local clients="$1"
  if [[ -n "${CONNECT_CONCURRENCY}" ]]; then
    printf '%s' "${CONNECT_CONCURRENCY}"
  elif [[ "${clients}" -ge 10000 ]]; then
    printf '1024'
  else
    printf '128'
  fi
}

append_multi_socket_args() {
  local target_name="$1"
  local -n cmd_ref="${target_name}"
  if [[ -n "${SEND_HWM}" ]]; then
    cmd_ref+=(--send-hwm "${SEND_HWM}")
  fi
  if [[ -n "${RECV_HWM}" ]]; then
    cmd_ref+=(--recv-hwm "${RECV_HWM}")
  fi
  if [[ -n "${SNDBUF}" ]]; then
    cmd_ref+=(--sndbuf "${SNDBUF}")
  fi
  if [[ -n "${RCVBUF}" ]]; then
    cmd_ref+=(--rcvbuf "${RCVBUF}")
  fi
}

build_multi_role_cmd() {
  local target_name="$1"
  local -n cmd_ref="${target_name}"
  local role="$2"
  local endpoint="$3"
  local io_threads="$4"
  local concurrency="$5"

  cmd_ref=("${runner_prefix[@]}" "${RUNNER}" "--multi-${role}" "${pattern}" "${transport}" "${size}" \
    --endpoint "${endpoint}" --clients "${pattern_clients}" \
    --duration "${DURATION}" --control-port 0 \
    --io-threads "${io_threads}" \
    --sndtimeo "${SNDTIMEO_MS}" --rcvtimeo "${RCVTIMEO_MS}" \
    --monitor-hwm "${MONITOR_HWM}" --connect-ready-timeout-ms "${CONNECT_READY_TIMEOUT_MS}" \
    --connect-concurrency "${concurrency}")
  append_multi_socket_args "${target_name}"
}

CASE_STATUS=""
CASE_METRIC_LOG=""

run_stream_case() {
  local concurrency="$1"
  local fifo="${RESULTS_ROOT}/multi/tmp/stream_control_${transport}_${size}.fifo"
  local endpoint
  local stream_server_cmd=()

  CASE_STATUS=""
  CASE_METRIC_LOG=""
  rm -f "${fifo}"
  mkfifo "${fifo}"
  endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
  build_multi_role_cmd stream_server_cmd "server" "${endpoint}" "${pattern_server_io_threads}" "${concurrency}"
  "${stream_server_cmd[@]}" <"${fifo}" >"${server_log}" 2>&1 &
  local server_pid=$!
  exec 3>"${fifo}"
  if ! wait_for_log_token "${server_log}" "READY," "${SERVER_READY_TIMEOUT_MS}" >/dev/null; then
    record_failure "${pattern}" "${transport}" "${size}" "${run}" "server_ready_timeout"
    wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "stream server" || true
    exec 3>&-
    rm -f "${fifo}"
    CASE_STATUS="fail"
    return 0
  fi

  local stream_client_rc=0
  "${stream_client_prefix[@]}" "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM \
    --sizes "${size}" --runs 1 --duration "${DURATION}" \
    --ccu "${pattern_clients}" --send-stop-token 1 --endpoint "${endpoint}" \
    >"${client_log}" 2>&1 || stream_client_rc=$?
  printf 'STOP\n' >&3
  exec 3>&-
  local server_exit=0
  wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "stream server" || server_exit=$?
  rm -f "${fifo}"
  append_auto_hwm_details "${server_log}"
  append_auto_hwm_details "${client_log}"

  if [[ "${stream_client_rc}" -ne 0 ]]; then
    record_failure "${pattern}" "${transport}" "${size}" "${run}" \
      "stream_client_exit_${stream_client_rc}"
    CASE_STATUS="fail"
    return 0
  fi
  if [[ "${server_exit}" -ne 0 ]]; then
    record_failure "${pattern}" "${transport}" "${size}" "${run}" \
      "stream_server_exit_${server_exit}"
    CASE_STATUS="fail"
    return 0
  fi

  local status_record
  status_record="$(case_status "${pattern}" "${transport}" "${size}" "${client_log}")"
  case "${status_record%%,*}" in
    unsupported)
      CASE_STATUS="unsupported"
      return 0
      ;;
    fail)
      record_failure "${pattern}" "${transport}" "${size}" "${run}" "${status_record#*,}"
      CASE_STATUS="fail"
      return 0
      ;;
  esac

  CASE_STATUS="ok"
  CASE_METRIC_LOG="${client_log}"
}

run_socket_case() {
  local concurrency="$1"
  local endpoint
  local server_fifo="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_server.fifo"
  local client_fifo="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_client.fifo"
  local server_cmd=()
  local client_cmd=()
  local server_pid
  local client_pid
  local server_fd
  local client_fd
  local metric_log="${server_log}"

  CASE_STATUS=""
  CASE_METRIC_LOG=""
  endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
  rm -f "${server_fifo}" "${client_fifo}"
  mkfifo "${server_fifo}" "${client_fifo}"
  build_multi_role_cmd server_cmd "server" "${endpoint}" "${pattern_server_io_threads}" "${concurrency}"
  "${server_cmd[@]}" <"${server_fifo}" >"${server_log}" 2>&1 &
  server_pid=$!
  exec {server_fd}>"${server_fifo}"
  if ! wait_for_log_token "${server_log}" "READY," "${SERVER_READY_TIMEOUT_MS}" >/dev/null; then
    record_failure "${pattern}" "${transport}" "${size}" "${run}" "server_ready_timeout"
    wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server" || true
    exec {server_fd}>&-
    rm -f "${server_fifo}" "${client_fifo}"
    CASE_STATUS="fail"
    return 0
  fi
  local server_control_endpoint=""
  if is_spot_control_pattern "${bare_pattern}"; then
    local control_line
    control_line="$(wait_for_log_token "${server_log}" "CONTROL_READY," "${SERVER_READY_TIMEOUT_MS}" || true)"
    if [[ "${control_line}" != CONTROL_READY,* ]]; then
      record_failure "${pattern}" "${transport}" "${size}" "${run}" "control_ready_timeout"
      wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server" || true
      exec {server_fd}>&-
      rm -f "${server_fifo}" "${client_fifo}"
      CASE_STATUS="fail"
      return 0
    fi
    server_control_endpoint="${control_line#CONTROL_READY,}"
  fi

  build_multi_role_cmd client_cmd "client" "${endpoint}" "${pattern_client_io_threads}" "${concurrency}"
  "${client_cmd[@]}" <"${client_fifo}" >"${client_log}" 2>&1 &
  client_pid=$!
  exec {client_fd}>"${client_fifo}"
  if is_spot_control_pattern "${bare_pattern}"; then
    local client_control_line
    client_control_line="$(wait_for_log_token "${client_log}" "CLIENT_CONTROL_ENDPOINT," "${CONNECT_READY_TIMEOUT_MS}" || true)"
    if [[ "${client_control_line}" != CLIENT_CONTROL_ENDPOINT,* ]]; then
      record_failure "${pattern}" "${transport}" "${size}" "${run}" "client_control_endpoint_timeout"
      wait_for_pid_or_kill "${client_pid}" "$(( (DURATION + 20) * 1000 ))" "client" || true
      wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server" || true
      exec {client_fd}>&-
      exec {server_fd}>&-
      rm -f "${server_fifo}" "${client_fifo}"
      CASE_STATUS="fail"
      return 0
    fi
    local client_control_endpoint="${client_control_line#CLIENT_CONTROL_ENDPOINT,}"
    printf 'CONNECT_CONTROL,%s\n' "${client_control_endpoint}" >&${server_fd}
    local connected_line
    connected_line="$(wait_for_log_token "${server_log}" "CONTROL_CONNECTED,${client_control_endpoint}" "${CONNECT_READY_TIMEOUT_MS}" || true)"
    if [[ "${connected_line}" != "CONTROL_CONNECTED,${client_control_endpoint}" ]]; then
      record_failure "${pattern}" "${transport}" "${size}" "${run}" "control_connected_timeout"
      wait_for_pid_or_kill "${client_pid}" "$(( (DURATION + 20) * 1000 ))" "client" || true
      wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server" || true
      exec {client_fd}>&-
      exec {server_fd}>&-
      rm -f "${server_fifo}" "${client_fifo}"
      CASE_STATUS="fail"
      return 0
    fi
    printf '%s\n' "${connected_line}" >&${client_fd}
  fi
  if is_start_gated_pattern "${bare_pattern}"; then
    if ! wait_for_log_token "${client_log}" "CLIENT_READY,${size}" "${CONNECT_READY_TIMEOUT_MS}" >/dev/null; then
      record_failure "${pattern}" "${transport}" "${size}" "${run}" "client_ready_timeout"
      wait_for_pid_or_kill "${client_pid}" "$(( (DURATION + 20) * 1000 ))" "client" || true
      wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server" || true
      exec {client_fd}>&-
      exec {server_fd}>&-
      rm -f "${server_fifo}" "${client_fifo}"
      CASE_STATUS="fail"
      return 0
    fi
    printf 'START,%s\n' "${size}" >&${server_fd}
    printf 'START,%s\n' "${size}" >&${client_fd}
  fi

  local client_exit=0
  local server_exit=0
  wait_for_pid_or_kill "${client_pid}" "$(( (DURATION + 20) * 1000 ))" "client" || client_exit=$?
  exec {client_fd}>&-
  wait_for_pid_or_kill "${server_pid}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" "server" || server_exit=$?
  exec {server_fd}>&-
  rm -f "${server_fifo}" "${client_fifo}"
  append_auto_hwm_details "${server_log}"
  append_auto_hwm_details "${client_log}"

  if [[ "${client_exit}" -ne 0 ]]; then
    record_failure "${pattern}" "${transport}" "${size}" "${run}" "client_exit_${client_exit}"
    CASE_STATUS="fail"
    return 0
  fi
  if [[ "${server_exit}" -ne 0 ]]; then
    record_failure "${pattern}" "${transport}" "${size}" "${run}" "server_exit_${server_exit}"
    CASE_STATUS="fail"
    return 0
  fi

  if [[ "${bare_pattern}" == "DEALER_ROUTER" \
     || "${bare_pattern}" == "ROUTER_ROUTER" || "${bare_pattern}" == "PUBSUB" \
     || "${bare_pattern}" == "SPOT_REQREP" \
     || "${bare_pattern}" == "SPOT_SENDSEND" \
     || "${bare_pattern}" == "SPOT" ]]; then
    metric_log="${client_log}"
  fi
  local status_record
  status_record="$(case_status "${pattern}" "${transport}" "${size}" "${metric_log}")"
  case "${status_record%%,*}" in
    unsupported)
      CASE_STATUS="unsupported"
      return 0
      ;;
    fail)
      record_failure "${pattern}" "${transport}" "${size}" "${run}" "${status_record#*,}"
      CASE_STATUS="fail"
      return 0
      ;;
  esac

  CASE_STATUS="ok"
  CASE_METRIC_LOG="${metric_log}"
}

merge_spot_clean_latency_log() {
  local active_log="$1"
  local latency_log="$2"
  local merged_log="$3"
  python3 - "$bare_pattern" "$transport" "$size" "$active_log" "$latency_log" "$merged_log" <<'PY'
import sys

pattern, transport, size, active_log, latency_log, merged_log = sys.argv[1:]
latency_metrics = {"latency", "latency_p95", "latency_p99"}
merged = {}

def load(path, allowed):
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            if not line.startswith("RESULT,"):
                continue
            parts = line.split(",")
            if len(parts) != 7:
                continue
            _, _, row_pattern, row_transport, row_size, metric, value = parts
            if row_pattern == pattern and row_transport == transport and row_size == size and metric in allowed:
                merged[metric] = value

load(active_log, {"throughput", "bandwidth", "latency", "latency_p95", "latency_p99"})
load(latency_log, latency_metrics)

with open(merged_log, "w", encoding="utf-8") as handle:
    for metric in ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]:
        if metric in merged:
            handle.write(f"RESULT,current,{pattern},{transport},{size},{metric},{merged[metric]}\n")
PY
}

run_spot_case_with_optional_clean_latency() {
  run_socket_case "$1"
  if [[ "${CASE_STATUS}" != "ok" || "${bare_pattern}" != "SPOT" \
     || "${SPOT_CLEAN_LATENCY}" == "0" ]]; then
    return 0
  fi

  local active_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_active.log"
  local latency_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_latency.log"
  local merged_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_merged.log"
  local failure_mark
  cp -f "${CASE_METRIC_LOG}" "${active_log}"
  sleep_ms "${RUN_COOLDOWN_MS}"
  failure_mark="$(wc -c < "${tmp_failures}")"
  PERF_MULTI_SPOT_LATENCY_ONLY=1 run_socket_case "$1"
  if [[ "${CASE_STATUS}" != "ok" ]]; then
    truncate -s "${failure_mark}" "${tmp_failures}"
    echo "      warning: MULTI_SPOT clean latency pass failed; using active-pass latency" >&2
    CASE_STATUS="ok"
    CASE_METRIC_LOG="${active_log}"
    return 0
  fi
  cp -f "${CASE_METRIC_LOG}" "${latency_log}"
  merge_spot_clean_latency_log "${active_log}" "${latency_log}" "${merged_log}"
  CASE_METRIC_LOG="${merged_log}"
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
display_server_io_threads="${SERVER_IO_THREADS:-${COMMON_IO_THREADS:-4}}"
display_client_io_threads="${CLIENT_IO_THREADS:-${COMMON_IO_THREADS:-4}}"
if [[ "${PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}" == "1" \
  || "${PERF_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}" == "1" ]]; then
  display_hwm="${HWM:-manual-unset}"
  display_send_hwm="${SEND_HWM:-${HWM:-manual-unset}}"
  display_recv_hwm="${RECV_HWM:-${HWM:-manual-unset}}"
  display_sndbuf="${SNDBUF:-auto-hwm}"
  display_rcvbuf="${RCVBUF:-auto-hwm}"
else
  display_hwm="auto-hwm"
  display_send_hwm="auto-hwm"
  display_recv_hwm="auto-hwm"
  display_sndbuf="auto-hwm"
  display_rcvbuf="auto-hwm"
fi
skip_entries=()
run_patterns=()
for pattern in "${patterns[@]}"; do
  bare_pattern="${pattern#MULTI_}"
  pattern_clients="$(default_clients_for_pattern "${pattern}")"
  if ! ensure_nofile_limit "${pattern_clients}"; then
    skip_entries+=("${pattern}: nofile_guard_${NOFILE_SKIP_REASON}")
    continue
  fi
  if ! ensure_memory_budget "${pattern_clients}"; then
    skip_entries+=("${pattern}: memory_guard_${MEMORY_SKIP_REASON}")
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

if [[ "${#skip_entries[@]}" -gt 0 ]]; then
  echo
  echo "## Skips"
  printf '\n## Skips\n' >> "${tmp_progress}"
  for item in "${skip_entries[@]}"; do
    echo "- ${item}"
    printf -- '- %s\n' "${item}" >> "${tmp_progress}"
  done
  echo
  printf '\n' >> "${tmp_progress}"
fi

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
  pattern_server_io_threads="${SERVER_IO_THREADS:-${COMMON_IO_THREADS:-4}}"
  pattern_client_io_threads="${CLIENT_IO_THREADS:-${COMMON_IO_THREADS:-4}}"
  IFS=',' read -r -a msg_sizes <<< "$(trim_csv "${pattern_msg_sizes}")"

  for transport_index in "${!transports[@]}"; do
    transport="${transports[transport_index]}"
    echo "    Testing ${transport} | ${pattern_msg_sizes}:"
    printf '    Testing %s | %s:\n' "${transport}" "${pattern_msg_sizes}" >> "${tmp_progress}"
    print_table_header "      "
    print_table_header "      " >> "${tmp_progress}"
    transport_failures=0
    transport_unsupported=0
    for size in "${msg_sizes[@]}"; do
      for ((run=1; run<=RUNS; run++)); do
        case_connect_concurrency="$(resolve_case_connect_concurrency "${pattern_clients}")"
        expected_result_lines=$((expected_result_lines + 5))
        if (( RUNS > 1 )); then
          printf '      run %s/%s:\n' "${run}" "${RUNS}"
          printf '      run %s/%s:\n' "${run}" "${RUNS}" >> "${tmp_progress}"
        fi
        server_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_server.log"
        client_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_client.log"
        rm -f "${server_log}" "${client_log}"

        if [[ "${bare_pattern}" == "STREAM" ]]; then
          run_stream_case "${case_connect_concurrency}"
        elif [[ "${bare_pattern}" == "SPOT" ]]; then
          run_spot_case_with_optional_clean_latency "${case_connect_concurrency}"
        else
          run_socket_case "${case_connect_concurrency}"
        fi

        case "${CASE_STATUS}" in
          unsupported)
            expected_result_lines=$((expected_result_lines - 5))
            transport_unsupported=1
            break
            ;;
          fail)
            transport_failures=$((transport_failures + 1))
            continue
            ;;
        esac
        if ! append_metrics "${pattern}" "${transport}" "${size}" "${run}" "${CASE_METRIC_LOG}"; then
          record_failure "${pattern}" "${transport}" "${size}" "${run}" "missing_required_result_lines"
          transport_failures=$((transport_failures + 1))
          continue
        fi
        row="$(format_progress_row "${bare_pattern}" "${transport}" "${size}" "${CASE_METRIC_LOG}" "      ")"
        echo "${row}"
        echo "${row}" >> "${tmp_progress}"
        if (( run < RUNS )); then
          echo "[cooldown ${RUN_COOLDOWN_MS}ms]"
          sleep_ms "${RUN_COOLDOWN_MS}"
        fi
      done
      if (( RUNS > 1 )); then
        if row="$(format_median_progress_row "${pattern}" "${transport}" "${size}" "${tmp_metrics}" "      median: ")"; then
          echo "${row}"
          echo "${row}" >> "${tmp_progress}"
        fi
      fi
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

python_status=0
python3 - "${ROOT_DIR}/report_common.py" "${tmp_metrics}" "${tmp_failures}" "${tmp_auto_hwm}" "${report}" "${requested_patterns}" "${TRANSPORTS}" "${display_msg_sizes}" \
  "${display_clients}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" \
  "${PIN_CPU}" "${display_server_io_threads}" "${display_client_io_threads}" \
  "${display_hwm}" "${display_send_hwm}" "${display_recv_hwm}" "${SNDTIMEO_MS}" "${RCVTIMEO_MS}" \
  "${CONNECT_READY_TIMEOUT_MS}" "${MONITOR_HWM}" "${SERVER_BIND_PORT}" \
  "${CONNECT_CONCURRENCY}" "${display_sndbuf}" "${display_rcvbuf}" \
  "${CTX_AUTO_HWM_ENABLE}" "${CTX_AUTO_HWM_PROFILE}" \
  "${STREAM_DEFAULT_CLIENTS}" "${SERVICE_CLIENTS}" \
  "${SERVER_READY_TIMEOUT_MS}" "${SERVER_SHUTDOWN_TIMEOUT_MS}" \
  "${TRANSPORT_TRANSITION_MS}" "${PATTERN_TRANSITION_MS}" \
  "${LAT_TIMEOUT_MS}" "${STREAM_NON_TCP_CLIENTS_MAX}" \
  "${DISABLE_RESOURCE_METRICS}" "${TIMEOUT_SECONDS}" \
  "${tmp_progress}" "${expected_result_lines}" "${actual_result_lines}" <<'PY' || python_status=$?
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

helper_path, metrics_path, failures_path, auto_hwm_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, clients, runs, duration, results_tag, pin_cpu, server_io_threads, client_io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, connect_ready_timeout_ms, monitor_hwm, server_bind_port, connect_concurrency, sndbuf, rcvbuf, ctx_auto_hwm_enable, ctx_auto_hwm_profile, default_stream_clients, service_clients, server_ready_timeout_ms, server_shutdown_timeout_ms, transport_transition_ms, pattern_transition_ms, lat_timeout_ms, stream_non_tcp_clients_max, disable_resource_metrics, timeout_seconds, progress_path, expected_result_lines, actual_result_lines = sys.argv[1:]
sys.path.insert(0, str(Path(helper_path).resolve().parent))
from report_common import emit_completion, emit_effective_options, emit_failures, load_failures, write_report

runs = int(runs)
expected_result_lines = int(expected_result_lines)
actual_result_lines = int(actual_result_lines)
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

failures = load_failures(failures_path)
auto_hwm_rows = []
with open(auto_hwm_path, encoding="utf-8", errors="replace") as f:
    for raw in f:
        line = raw.strip()
        if not line.startswith("AUTO_HWM_DETAIL,"):
            continue
        fields = {}
        for item in line.split(",")[1:]:
            if "=" not in item:
                continue
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
        if fields:
            auto_hwm_rows.append(fields)

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
    return f"{value / 1000.0:.3f}"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} MB/s"

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} ms"

def fmt_size(size):
    return f"{size}B"

def parse_int(value, default=0):
    try:
        return int(str(value))
    except (TypeError, ValueError):
        return default

def bytes_to_kb(value):
    parsed = parse_int(value, -1)
    if parsed < 0:
        return "?"
    if parsed == 0:
        return "0"
    if parsed % 1024 == 0:
        return str(parsed // 1024)
    return f"{parsed / 1024.0:.1f}"

def emit_table(indent, columns, table_rows):
    widths = []
    for header, key in columns:
        width = len(header)
        for row in table_rows:
            width = max(width, len(str(row.get(key, "?"))))
        widths.append(width)
    emit(indent + "| " + " | ".join(
        f"{columns[i][0]:<{widths[i]}}" for i in range(len(columns))
    ) + " |")
    emit(indent + "|-" + "-|-".join("-" * width for width in widths) + "-|")
    for row in table_rows:
        emit(indent + "| " + " | ".join(
            f"{str(row.get(columns[i][1], '?')):<{widths[i]}}"
            for i in range(len(columns))
        ) + " |")

def auto_hwm_pattern_rows(pattern):
    return [
        row for row in auto_hwm_rows
        if row.get("pattern", "").upper() == pattern.upper()
    ]

def emit_multi_auto_hwm(pattern):
    selected = auto_hwm_pattern_rows(pattern)
    if not selected:
        return
    if pattern in {"MULTI_SPOT", "MULTI_SPOT_REQREP", "MULTI_SPOT_SENDSEND"}:
        emit_spot_auto_hwm(selected)
    else:
        emit_non_spot_auto_hwm(selected)

def emit_non_spot_auto_hwm(rows_for_pattern):
    display_rows = []
    seen = set()
    for row in rows_for_pattern:
        if not row.get("msg_size") or row.get("msg_size") == "0":
            continue
        display = dict(row)
        display["type"] = row.get("socket_type", "")
        display["unit_budget_kb"] = bytes_to_kb(row.get("unit_budget_bytes", ""))
        display["effective_sndbuf_kb"] = bytes_to_kb(row.get("effective_sndbuf", ""))
        display["effective_rcvbuf_kb"] = bytes_to_kb(row.get("effective_rcvbuf", ""))
        key = tuple(display.get(name, "") for name in (
            "msg_size", "component", "type", "unit_budget_kb",
            "effective_message_bytes", "sndhwm", "rcvhwm",
            "effective_sndbuf_kb", "effective_rcvbuf_kb",
        ))
        if key in seen:
            continue
        seen.add(key)
        display_rows.append(display)
    if not display_rows:
        return
    display_rows.sort(key=lambda row: (
        parse_int(row.get("msg_size", "0")),
        row.get("component", ""),
        row.get("type", ""),
    ))
    emit("    Auto-HWM detail:")
    emit_table("      ", (
        ("Size(B)", "msg_size"),
        ("Component", "component"),
        ("Type", "type"),
        ("UnitBudget(KB)", "unit_budget_kb"),
        ("MsgUnit(B)", "effective_message_bytes"),
        ("SNDHWM", "sndhwm"),
        ("RCVHWM", "rcvhwm"),
        ("SNDBUF(KB)", "effective_sndbuf_kb"),
        ("RCVBUF(KB)", "effective_rcvbuf_kb"),
    ), display_rows)
    emit("")

def emit_spot_auto_hwm(rows_for_pattern):
    def build(owner):
        out = []
        seen = set()
        for row in rows_for_pattern:
            if row.get("source") != "spotnode_snapshot":
                continue
            if row.get("owner") != owner:
                continue
            display = dict(row)
            display["class"] = row.get("policy_class", "")
            display["cap"] = row.get("size_cap", "")
            display["slots"] = row.get("socket_message_slots", "")
            key = tuple(display.get(name, "") for name in (
                "msg_size", "effective_message_bytes", "socket",
                "socket_type", "profile", "class", "role", "cap", "slots",
                "sndhwm", "rcvhwm",
            ))
            if key in seen:
                continue
            seen.add(key)
            out.append(display)
        out.sort(key=lambda row: (
            parse_int(row.get("msg_size", "0")),
            row.get("socket", ""),
            row.get("role", ""),
        ))
        return out
    columns = (
        ("Size(B)", "msg_size"),
        ("MsgUnit(B)", "effective_message_bytes"),
        ("Socket", "socket"),
        ("Type", "socket_type"),
        ("Profile", "profile"),
        ("Class", "class"),
        ("Role", "role"),
        ("Cap", "cap"),
        ("Slots", "slots"),
        ("SNDHWM", "sndhwm"),
        ("RCVHWM", "rcvhwm"),
    )
    node_rows = build("node")
    spot_rows = build("spot")
    if node_rows:
        emit("    Auto-HWM spotnode:")
        emit_table("      ", columns, node_rows)
    if spot_rows:
        emit("    Auto-HWM spot handles:")
        emit_table("      ", columns, spot_rows)
    if node_rows or spot_rows:
        emit("")

lines = []

def emit(line=""):
    lines.append(line)

start_options = [
    ("runs", runs),
    ("patterns", pattern_csv),
    ("transports", transports_csv),
    ("msg_sizes", msg_sizes_csv),
    ("duration_seconds", duration),
    ("clients", clients),
    ("default_clients", "100"),
    ("default_stream_clients", default_stream_clients),
    ("service_clients", service_clients),
    ("pin_cpu", "on" if pin_cpu == "1" else "off"),
    ("server_io_threads", server_io_threads),
    ("client_io_threads", client_io_threads),
    ("hwm", hwm or "auto-hwm"),
    ("sndhwm", send_hwm or "auto-hwm"),
    ("rcvhwm", recv_hwm or "auto-hwm"),
    ("sndbuf", sndbuf or "auto-hwm"),
    ("rcvbuf", rcvbuf or "auto-hwm"),
    ("ctx_auto_hwm_enable", ctx_auto_hwm_enable),
    ("ctx_auto_hwm_profile", ctx_auto_hwm_profile),
    ("sndtimeo_ms", sndtimeo_ms),
    ("rcvtimeo_ms", rcvtimeo_ms),
    ("connect_concurrency", connect_concurrency or "auto"),
    ("connect_ready_timeout_ms", connect_ready_timeout_ms),
    ("monitor_hwm", monitor_hwm),
    ("server_ready_timeout_ms", server_ready_timeout_ms),
    ("server_shutdown_timeout_ms", server_shutdown_timeout_ms),
    ("server_bind_port", server_bind_port),
    ("transport_transition_ms", transport_transition_ms),
    ("pattern_transition_ms", pattern_transition_ms),
    ("lat_timeout_ms", lat_timeout_ms),
    ("stream_non_tcp_clients_max", stream_non_tcp_clients_max),
    ("disable_resource_metrics", disable_resource_metrics),
    ("timeout_seconds", timeout_seconds),
]
if results_tag:
    start_options.append(("results_tag", results_tag))

emit_effective_options(lines, "start", "java", "multi", start_options)
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
        emit("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |")
        emit("|----------|--------------------|----------------|---------------|---------------|---------------|")
        rate_unit = "Kops/s" if pattern in {"MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER", "MULTI_SPOT_REQREP", "MULTI_SPOT_SENDSEND", "MULTI_STREAM"} else "Kmsg/s"
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in all_metrics}
            throughput = f"{fmt_rate(metric_values['throughput']):>8} {rate_unit}"
            emit(
                f"| {fmt_size(size):<8} | {throughput:>16} | "
                f"{fmt_bandwidth(metric_values['bandwidth']):>12} | "
                f"{fmt_latency_ms(metric_values['latency']):>12} | "
                f"{fmt_latency_ms(metric_values['latency_p95']):>12} | "
                f"{fmt_latency_ms(metric_values['latency_p99']):>12} |"
            )
            for metric in all_metrics:
                if rows[key].get(metric):
                    result_lines.append(
                        f"RESULT,current,{pattern},{transport},{size},{metric},{fmt_metric(metric_values[metric])}"
                    )
        emit("")
    emit_multi_auto_hwm(pattern)

emit_effective_options(lines, "result", "java", "multi", start_options)
emit("")
emit("## Result Data")
for line in result_lines:
    emit(line)
emit("")
status = "complete" if expected_result_lines == actual_result_lines and not failures else "partial"
emit_completion(lines, status, expected_result_lines, actual_result_lines)
emit_failures(lines, failures)
text = write_report(lines, report_path)
sys.stdout.write(text)
sys.exit(0 if status == "complete" else 1)
PY

prune_reports "${RESULTS_ROOT}/multi/report"
echo "saved report: ${report}"
exit "${python_status}"
