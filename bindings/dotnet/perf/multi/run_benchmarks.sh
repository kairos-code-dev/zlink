#!/usr/bin/env bash
set -euo pipefail
trap '' PIPE

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${DOTNET_DIR}/perf/common/report_helpers.sh"
PROJECT="${DOTNET_DIR}/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj"
PROJECT_DIR="${DOTNET_DIR}/perf/multi/Zlink.BindingBench.Multi"
REPO_DIR="$(cd "${DOTNET_DIR}/../.." && pwd)"
STREAM_CLIENT="${REPO_DIR}/bindings/c/build/perf/perf_stream_client"
STREAM_BUILD_DIR="${REPO_DIR}/bindings/c/build"
VERSION_FILE="${REPO_DIR}/VERSION"
CORE_LIB_DIR="${REPO_DIR}/core/build/lib"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so.${CORE_VERSION}"
RESULTS_ROOT="${DOTNET_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS="${PERF_TRANSPORTS:-tcp,tls,ws,wss}"
MSG_SIZES="${PERF_MSG_SIZES:-}"
CLIENTS="${PERF_MULTI_CLIENTS:-}"
EFFECTIVE_DEFAULT_CLIENTS="${PERF_MULTI_DEFAULT_CLIENTS:-${PERF_DEFAULT_CLIENTS:-100}}"
EFFECTIVE_DEFAULT_STREAM_CLIENTS="${PERF_MULTI_DEFAULT_STREAM_CLIENTS:-${PERF_STREAM_DEFAULT_CLIENTS:-10000}}"
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
RUNS="${PERF_RUNS:-1}"
READY_TIMEOUT_MS="${PERF_MULTI_CONNECT_READY_TIMEOUT_MS:-5000}"
SERVER_READY_TIMEOUT_MS="${PERF_MULTI_SERVER_READY_TIMEOUT_MS:-10000}"
SERVER_SHUTDOWN_TIMEOUT_MS="${PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-5000}"
RESULT_TIMEOUT_SECONDS="${PERF_MULTI_TIMEOUT_SECONDS:-60}"
TIMEOUT_SECONDS_DISPLAY="${PERF_MULTI_TIMEOUT_SECONDS:-${PERF_TIMEOUT_SECONDS:-auto}}"
CASE_COOLDOWN_MS="${PERF_MULTI_CASE_COOLDOWN_MS:-0}"
TRANSPORT_TRANSITION_MS="${PERF_MULTI_TRANSPORT_TRANSITION_MS:-3000}"
PATTERN_TRANSITION_MS="${PERF_MULTI_PATTERN_TRANSITION_MS:-3000}"
RESULTS_TAG=""
CONFIGURATION="${PERF_CONFIGURATION:-Release}"
REPORT=""
REUSE_BUILD=0
CLEAN_BUILD=0
BUILD_DIR=""
OUTPUT_PATH=""
PIN_CPU=0
COMMON_IO_THREADS="${PERF_IO_THREADS:-}"
SERVER_IO_THREADS="${PERF_MULTI_SERVER_IO_THREADS:-${PERF_SERVER_IO_THREADS:-}}"
CLIENT_IO_THREADS="${PERF_MULTI_CLIENT_IO_THREADS:-${PERF_CLIENT_IO_THREADS:-}}"
HWM="${PERF_MULTI_HWM:-${PERF_HWM:-}}"
SNDHWM="${PERF_MULTI_SNDHWM:-${PERF_SNDHWM:-}}"
RCVHWM="${PERF_MULTI_RCVHWM:-${PERF_RCVHWM:-}}"
SNDBUF="${PERF_MULTI_SNDBUF:-${PERF_SNDBUF:-}}"
RCVBUF="${PERF_MULTI_RCVBUF:-${PERF_RCVBUF:-}}"
SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-${PERF_SNDTIMEO_MS:-200}}"
RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-${PERF_RCVTIMEO_MS:-200}}"
CONNECT_CONCURRENCY="${PERF_MULTI_CONNECT_CONCURRENCY:-${PERF_CONNECT_CONCURRENCY:-}}"
MONITOR_HWM="${PERF_MULTI_MONITOR_HWM:-${PERF_MONITOR_HWM:-1000}}"
SERVER_BIND_PORT="${PERF_MULTI_SERVER_BIND_PORT:-${PERF_SERVER_BIND_PORT:-0}}"
CTX_AUTO_HWM_ENABLE="${PERF_CTX_AUTO_HWM_ENABLE:-1}"
CTX_AUTO_HWM_PROFILE="${PERF_MULTI_CTX_AUTO_HWM_PROFILE:-${PERF_CTX_AUTO_HWM_PROFILE:-balanced}}"
ALLOW_MANUAL_SOCKET_OVERRIDES="${PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES:-${PERF_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}}"
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

prune_report_dir() {
  local report_dir="$1"
  local max_files="${2:-100}"
  python3 - "${report_dir}" "${max_files}" <<'PY'
import pathlib
import sys

report_dir = pathlib.Path(sys.argv[1])
max_files = int(sys.argv[2])
if max_files <= 0 or not report_dir.exists():
    raise SystemExit(0)

files = sorted(
    [p for p in report_dir.iterdir() if p.is_file()],
    key=lambda p: p.name,
)
overflow = len(files) - max_files
for path in files[:max(0, overflow)]:
    try:
        path.unlink()
    except FileNotFoundError:
        pass
PY
}

resolve_perf_binary() {
  local project_dir="$1"
  local assembly_name="$2"
  local binary_path="${project_dir}/bin/${CONFIGURATION}/net8.0/${assembly_name}"
  local dll_path="${project_dir}/bin/${CONFIGURATION}/net8.0/${assembly_name}.dll"
  if [[ -x "${binary_path}" ]]; then
    printf '%s' "${binary_path}"
    return 0
  fi
  if [[ -f "${dll_path}" ]]; then
    printf 'dotnet %q' "${dll_path}"
    return 0
  fi
  return 1
}

usage() {
  cat <<'USAGE'
Usage: perf/multi/run_benchmarks.sh [options]

Run .NET multi-socket benchmark patterns.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --duration N          Active duration seconds (default: 5).
  --msg-sizes LIST      Message size list.
  --transports LIST     Transport list override (default: tcp,tls,ws,wss).
  --clients N           Client socket count (default: 100, stream=10000).
  --runs N              Iterations per configuration (default: 1).
  --build-dir PATH      Accepted for policy compatibility.
  --reuse-build         Reuse existing build output.
  --clean-build         Remove project bin/obj before build.
  --output PATH         Tee report output to PATH.
  --pin-cpu             Pin benchmark processes to CPU 1 on Linux.
  --io-threads N        Set both server/client io threads.
  --server-io-threads N Server io threads override.
  --client-io-threads N Client io threads override.
  --hwm N               Shared HWM fallback.
  --send-hwm N          Send HWM override.
  --recv-hwm N          Receive HWM override.
  --buf SIZE            Send/receive buffer override.
  --sndbuf SIZE         Send buffer override.
  --rcvbuf SIZE         Receive buffer override.
  --sndtimeo N          Send timeout ms.
  --rcvtimeo N          Receive timeout ms.
  --send-timeout-ms N   Alias of --sndtimeo.
  --recv-timeout-ms N   Alias of --rcvtimeo.
  --connect-concurrency N Client connect concurrency.
  --transport-transition-ms N Transport cooldown.
  --pattern-transition-ms N Pattern cooldown.
  --server-ready-timeout-ms N Server ready timeout.
  --connect-ready-timeout-ms N Client connect-ready timeout.
  --monitor-hwm N       Monitor socket HWM.
  --server-shutdown-timeout-ms N Server shutdown timeout.
  --server-bind-port N  Fixed bind port (0=auto).
  --auto-hwm-profile NAME Auto-HWM profile.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional report suffix tag.

Notes:
  - result is saved under results/multi/report/ as
    perf_dotnet_multi_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt.
USAGE
}

ensure_build_output() {
  if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
    rm -rf "${PROJECT_DIR}/bin" "${PROJECT_DIR}/obj"
  fi
  if [[ "${REUSE_BUILD}" -eq 1 ]]; then
    return
  fi

  dotnet build "${PROJECT}" -c "${CONFIGURATION}" >/dev/null
}

sync_native_dirs() {
  local search_root="$1"
  [[ -d "${search_root}" ]] || return 0

  while IFS= read -r native_dir; do
    rm -f "${native_dir}/libzlink.so" \
      "${native_dir}/libzlink.so.6" \
      "${native_dir}/libzlink.so."*
    cp -f "${CORE_LIB}" "${native_dir}/libzlink.so.${CORE_VERSION}"
    ln -sfn "libzlink.so.${CORE_VERSION}" "${native_dir}/libzlink.so.6"
    ln -sfn libzlink.so.6 "${native_dir}/libzlink.so"
  done < <(find "${search_root}" -type d -path '*linux-x64/native')
}

prepare_core_runtime() {
  if [[ ! -f "${CORE_LIB}" ]]; then
    echo "core runtime not found: ${CORE_LIB}" >&2
    echo "Build core/build before running dotnet perf." >&2
    exit 1
  fi
  export ZLINK_LIBRARY_PATH="${CORE_LIB}"
  sync_native_dirs "${PROJECT_DIR}/bin"
}

ensure_stream_client() {
  if [[ -x "${STREAM_CLIENT}" ]]; then
    return
  fi

  cmake -S "${REPO_DIR}/bindings/c" -B "${STREAM_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LTO=OFF >/dev/null
  cmake --build "${STREAM_BUILD_DIR}" --target perf_stream_client >/dev/null
}

normalize_platform() {
  case "$(uname -s)" in
    Linux*) printf 'linux' ;;
    Darwin*) printf 'macos' ;;
    MINGW*|MSYS*|CYGWIN*) printf 'windows' ;;
    *) uname -s | tr '[:upper:]' '[:lower:]' ;;
  esac
}

print_line() {
  local line="${1:-}"
  if [[ -n "${OUTPUT_PATH}" ]]; then
    printf '%s\n' "${line}" | tee -a "${REPORT}" "${OUTPUT_PATH}"
  else
    printf '%s\n' "${line}" | tee -a "${REPORT}"
  fi
}

sleep_ms() {
  local ms="${1:-0}"
  sleep "$(printf '%d.%03d' "$(( ms / 1000 ))" "$(( ms % 1000 ))")"
}

validate_uint() {
  local label="${1:-value}"
  local value="${2:-}"
  if [[ ! "${value}" =~ ^[0-9]+$ || "${value}" -lt 1 ]]; then
    echo "${label} must be a positive integer." >&2
    exit 1
  fi
}

validate_nonnegative_uint() {
  local label="${1:-value}"
  local value="${2:-}"
  if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
    echo "${label} must be a non-negative integer." >&2
    exit 1
  fi
}

validate_byte_size_token() {
  local label="${1:-value}"
  local value="${2:-}"
  if [[ -n "${value}" && ! "${value}" =~ ^[0-9]+([bBkKmMgG])?$ ]]; then
    echo "${label} must be a byte size token such as 64b, 1k, or 64k." >&2
    exit 1
  fi
}

require_arg() {
  local option="${1:-option}"
  local value="${2:-}"
  if [[ -z "${value}" || "${value}" == --* ]]; then
    echo "Error: ${option} requires a value." >&2
    exit 1
  fi
}

normalize_multi_pattern_csv() {
  local raw="${1:-}"

  python3 - "${raw}" <<'PY'
import sys

raw = sys.argv[1].upper()
allowed = {
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "SPOT",
    "SPOT_REQREP",
    "SPOT_SENDSEND",
    "STREAM",
}

if raw == "ALL":
    print(
        "MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,"
        "MULTI_PUBSUB,MULTI_SPOT,MULTI_SPOT_REQREP,"
        "MULTI_SPOT_SENDSEND,MULTI_STREAM"
    )
    raise SystemExit(0)

items = []
for token in raw.split(","):
    value = token.strip()
    if not value:
        continue
    if value.startswith("MULTI_"):
        value = value[len("MULTI_") :]
    if value == "STREAMS":
        value = "STREAM"
    if value not in allowed:
        raise SystemExit(f"unsupported multi pattern: {value}")
    items.append(f"MULTI_{value}")

if not items:
    raise SystemExit("no valid multi pattern specified")

print(",".join(items))
PY
}

effective_msg_sizes_display() {
  local patterns_csv="${1:-}"
  local explicit_sizes="${2:-}"
  if [[ -n "${explicit_sizes}" ]]; then
    printf '%s' "${explicit_sizes}"
    return
  fi

  python3 - "${patterns_csv}" <<'PY'
import sys

patterns = [item.strip() for item in sys.argv[1].split(",") if item.strip()]
sizes = set()
for pattern in patterns:
    if pattern == "MULTI_STREAM":
        sizes.update([64, 256, 1024, 65536])
    else:
        sizes.update([64, 256, 1024, 65536, 131072, 262144])
print(",".join(str(v) for v in sorted(sizes)))
PY
}

effective_clients_display() {
  local patterns_csv="${1:-}"
  local explicit_clients="${2:-}"
  if [[ -n "${explicit_clients}" ]]; then
    printf '%s' "${explicit_clients}"
    return
  fi

  python3 - "${patterns_csv}" <<'PY'
import sys

patterns = [item.strip() for item in sys.argv[1].split(",") if item.strip()]
if patterns and all(item == "MULTI_STREAM" for item in patterns):
    print("10000")
else:
    print("100")
PY
}

default_msg_sizes_for_pattern() {
  local pattern="${1:-}"
  if [[ "${pattern}" == "MULTI_STREAM" ]]; then
    printf '%s' "64,256,1024,65536"
  else
    printf '%s' "64,256,1024,65536,131072,262144"
  fi
}

default_clients_for_pattern() {
  local pattern="${1:-}"
  if [[ "${pattern}" == "MULTI_STREAM" ]]; then
    printf '%s' "${EFFECTIVE_DEFAULT_STREAM_CLIENTS}"
  else
    printf '%s' "${EFFECTIVE_DEFAULT_CLIENTS}"
  fi
}

pattern_uses_control_pipe() {
  local pattern="${1:-}"
  case "${pattern}" in
    MULTI_DEALER_DEALER|MULTI_PUBSUB|MULTI_SPOT|MULTI_SPOT_REQREP|MULTI_SPOT_SENDSEND|MULTI_STREAM)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

wait_for_ready_endpoint() {
  local log_path="$1"
  local timeout_ms="${2:-${SERVER_READY_TIMEOUT_MS}}"
  python3 - "${log_path}" "${timeout_ms}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + max(0, int(sys.argv[2])) / 1000.0
while time.time() < deadline:
    if path.exists():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if line.startswith("READY,"):
                print(line.split(",", 1)[1].strip())
                raise SystemExit(0)
            if "multi_server_error:" in line:
                raise SystemExit(1)
    time.sleep(0.05)
raise SystemExit(1)
PY
}

wait_for_client_ready_line() {
  local log_path="$1"
  local timeout_ms="${2:-${READY_TIMEOUT_MS}}"
  python3 - "${log_path}" "${timeout_ms}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + max(0, int(sys.argv[2])) / 1000.0
while time.time() < deadline:
    if path.exists():
        text = path.read_text(encoding="utf-8", errors="replace")
        if "CLIENT_READY," in text:
            raise SystemExit(0)
        if "multi_client_error:" in text:
            raise SystemExit(1)
    time.sleep(0.05)
raise SystemExit(1)
PY
}

wait_for_control_ready_endpoint() {
  local log_path="$1"
  local timeout_ms="${2:-${SERVER_READY_TIMEOUT_MS}}"
  python3 - "${log_path}" "${timeout_ms}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + max(0, int(sys.argv[2])) / 1000.0
while time.time() < deadline:
    if path.exists():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if line.startswith("CONTROL_READY,"):
                print(line.split(",", 1)[1].strip())
                raise SystemExit(0)
            if "multi_server_error:" in line:
                raise SystemExit(1)
    time.sleep(0.05)
raise SystemExit(1)
PY
}

wait_for_client_control_endpoint() {
  local log_path="$1"
  local timeout_ms="${2:-${READY_TIMEOUT_MS}}"
  python3 - "${log_path}" "${timeout_ms}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + max(0, int(sys.argv[2])) / 1000.0
while time.time() < deadline:
    if path.exists():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if line.startswith("CLIENT_CONTROL_ENDPOINT,"):
                print(line.split(",", 1)[1].strip())
                raise SystemExit(0)
        if "multi_client_error:" in text:
            raise SystemExit(1)
    time.sleep(0.05)
raise SystemExit(1)
PY
}

wait_for_control_connected() {
  local log_path="$1"
  local timeout_ms="${2:-${READY_TIMEOUT_MS}}"
  python3 - "${log_path}" "${timeout_ms}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + max(0, int(sys.argv[2])) / 1000.0
while time.time() < deadline:
    if path.exists():
        text = path.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if line.startswith("CONTROL_CONNECTED,"):
                print(line.split(",", 1)[1].strip())
                raise SystemExit(0)
    time.sleep(0.05)
raise SystemExit(1)
PY
}

wait_for_result_line() {
  local log_path="$1"
  local needle="$2"
  local timeout_seconds="$3"
  local deadline=$((SECONDS + timeout_seconds))
  while (( SECONDS < deadline )); do
    if [[ -f "${log_path}" ]] && grep -qF "${needle}" "${log_path}"; then
      return 0
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

terminate_pid() {
  local pid="$1"
  if ! kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi
  kill "${pid}" 2>/dev/null || true
  if wait_for_pid "${pid}" 2; then
    return 0
  fi
  kill -9 "${pid}" 2>/dev/null || true
  wait "${pid}" 2>/dev/null || true
}

shutdown_timeout_seconds() {
  printf '%s' "$(( (SERVER_SHUTDOWN_TIMEOUT_MS + 999) / 1000 ))"
}

write_control_line() {
  local fd="$1"
  shift
  printf "$@" >&${fd} 2>/dev/null || true
}

is_eaddrinuse_log() {
  local log="${1:-}"
  [[ -f "${log}" ]] || return 1
  grep -qi "errno 98\|address already in use" "${log}" 2>/dev/null
}

extract_results_from_logs() {
  local primary_log="${1:-}"
  local secondary_log="${2:-}"
  local pattern="${3:-}"
  local transport="${4:-}"
  local size="${5:-}"

  python3 - "${primary_log}" "${secondary_log}" "${pattern}" "${transport}" "${size}" <<'PY'
import csv
import sys
from pathlib import Path

primary = Path(sys.argv[1])
secondary = Path(sys.argv[2])
expected = sys.argv[3]
transport = sys.argv[4]
size = sys.argv[5]
base = expected[len("MULTI_") :] if expected.startswith("MULTI_") else expected
required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
merged = {}

for path in (primary, secondary):
    if not path.exists():
        continue
    with path.open(encoding="utf-8", errors="replace") as handle:
        reader = csv.reader(handle)
        for row in reader:
            if len(row) != 7 or row[0] != "RESULT" or row[1] not in {"dotnet", "current"}:
                continue
            if row[2] not in {expected, base} or row[3] != transport or row[4] != size:
                continue
            metric = row[5]
            if metric in required:
                row[1] = "dotnet"
                row[2] = expected
                merged[metric] = row

missing = [metric for metric in required if metric not in merged]
if missing:
    raise SystemExit("missing required metrics: " + ",".join(missing))

for metric in required:
    print(",".join(merged[metric]))
PY
}

emit_result_row() {
  local metrics_file="${1:-}"
  local pattern="${2:-}"

  if [[ ! -s "${metrics_file}" ]]; then
    return
  fi

  python3 - "${metrics_file}" "${pattern}" <<'PY'
import csv
import sys

pattern = sys.argv[2].upper()
echo_patterns = {
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_STREAM",
    "MULTI_SPOT_REQREP",
    "MULTI_SPOT_SENDSEND",
}
metrics = {}
size = ""
with open(sys.argv[1], encoding="utf-8") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7 or row[0] != "RESULT":
            continue
        size = row[4]
        metrics[row[5]] = row[6]

throughput_unit = "Kops/s" if pattern in echo_patterns else "Kmsg/s"
throughput = float(metrics["throughput"]) / 1000.0
bandwidth = float(metrics["bandwidth"])
latency_ms = float(metrics["latency"])
latency_p95_ms = float(metrics["latency_p95"])
latency_p99_ms = float(metrics["latency_p99"])
throughput_text = f"{throughput:8.3f} {throughput_unit}"
print(
    f"      | {size + 'B':<8} | {throughput_text:>16} | {bandwidth:>10.3f} MB/s |"
    f" {latency_ms:>9.3f} ms | {latency_p95_ms:>9.3f} ms | {latency_p99_ms:>9.3f} ms |"
)
PY
}

emit_failure_row() {
  local size="${1:-}"
  print_line "      | ${size}B      | FAIL               | FAIL           | FAIL          | FAIL          | FAIL          |"
}

extract_unsupported_line() {
  local pattern="${1:-}"
  local transport="${2:-}"
  shift 2

  python3 - "${pattern}" "${transport}" "$@" <<'PY'
import pathlib
import sys

expected = sys.argv[1]
transport = sys.argv[2]
base = expected[len("MULTI_"):] if expected.startswith("MULTI_") else expected
needles = {
    f"UNSUPPORTED,dotnet,{expected},{transport}",
    f"UNSUPPORTED,dotnet,{base},{transport}",
}
canonical = f"UNSUPPORTED,dotnet,{expected},{transport}"

for raw_path in sys.argv[3:]:
    path = pathlib.Path(raw_path)
    if not path.exists():
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if line.strip() in needles:
            print(canonical)
            raise SystemExit(0)
    if transport in {"tcp", "tls", "ws", "wss", "ipc"}:
        lowered = text.lower()
        if "errno 98" in lowered or "address already in use" in lowered:
            raise SystemExit(1)
        if ("permission denied" in lowered
                or "operation not permitted" in lowered
                or "zlinkconnectexception" in lowered
                or "errno 1" in lowered
                or "errno 13" in lowered):
            print(canonical)
            raise SystemExit(0)

raise SystemExit(1)
PY
}

emit_auto_hwm_detail_table() {
  local pattern_name="${1:-}"
  shift || true

  python3 - "${pattern_name}" "$@" <<'PY'
import pathlib
import sys

SPOT_CONTROL_PATTERNS = {"SPOT", "SPOT_REQREP", "SPOT_SENDSEND"}


def normalize_pattern(name):
    value = (name or "").strip().upper()
    if value.startswith("MULTI_"):
        value = value[6:]
    return value


def parse_int(value, default=0):
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return default


def bytes_to_kb(value):
    parsed = parse_int(value, -1)
    return "" if parsed < 0 else str(parsed // 1024)


def parse_detail_line(line):
    stripped = (line or "").strip()
    if not stripped.startswith("AUTO_HWM_DETAIL,"):
        return None
    fields = {}
    for item in stripped.split(",")[1:]:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        fields[key.strip()] = value.strip()
    return fields


def cell_widths(rows, columns):
    widths = []
    for header, key in columns:
        width = len(header)
        for row in rows:
            width = max(width, len(str(row.get(key, "?"))))
        widths.append(width)
    return widths


def emit_markdown_table(indent, columns, rows):
    widths = cell_widths(rows, columns)
    header_cells = [
        f" {header:<{widths[index]}} "
        for index, (header, _key) in enumerate(columns)
    ]
    sep_cells = ["-" * (width + 2) for width in widths]
    print(f"{indent}|" + "|".join(header_cells) + "|")
    print(f"{indent}|" + "|".join(sep_cells) + "|")
    for row in rows:
        cells = [
            f" {str(row.get(key, '?')):<{widths[index]}} "
            for index, (_header, key) in enumerate(columns)
        ]
        print(f"{indent}|" + "|".join(cells) + "|")


def active_hwm_fields(row):
    socket_type = (row.get("socket_type") or row.get("type") or "").lower()
    role = (row.get("role") or "").lower()
    send_active = True
    recv_active = True
    if socket_type in ("pub", "xpub") and role in ("spot_data", "control"):
        recv_active = False
    if socket_type in ("sub", "xsub") and role in ("recv_ingress", "control"):
        send_active = False
    return send_active, recv_active


def apply_active_hwm_display(row):
    display = dict(row)
    send_active, recv_active = active_hwm_fields(display)
    if not send_active:
        display["sndhwm"] = "-"
    if not recv_active:
        display["rcvhwm"] = "-"
    return display


def spot_snapshot_table(title, rows):
    if not rows:
        return False
    display_rows = []
    seen = set()
    for row in sorted(
        rows,
        key=lambda item: (
            parse_int(item.get("msg_size", "0")),
            parse_int(item.get("owner_id", "0")),
            item.get("socket", ""),
            item.get("role", ""),
        ),
    ):
        display = dict(row)
        display["type"] = row.get("socket_type", "")
        display = apply_active_hwm_display(display)
        key = tuple(
            display.get(name, "")
            for name in (
                "msg_size",
                "effective_message_bytes",
                "socket",
                "type",
                "role",
                "sndhwm",
                "rcvhwm",
                "effective_sndbuf",
                "effective_rcvbuf",
            )
        )
        if key in seen:
            continue
        seen.add(key)
        display_rows.append(display)
    if not display_rows:
        return False
    print(f"    {title}:")
    grouped = {}
    order = []
    for row in display_rows:
        key = (row.get("msg_size", ""), row.get("effective_message_bytes", ""))
        if key not in grouped:
            grouped[key] = []
            order.append(key)
        grouped[key].append(row)
    for index, key in enumerate(order):
        msg_size, msg_unit = key
        print(f"      - Size(B)={msg_size}, MsgUnit(B)={msg_unit}")
        emit_markdown_table(
            "      ",
            (
                ("Socket", "socket"),
                ("Type", "type"),
                ("Role", "role"),
                ("SNDHWM", "sndhwm"),
                ("RCVHWM", "rcvhwm"),
                ("SNDBUF", "effective_sndbuf"),
                ("RCVBUF", "effective_rcvbuf"),
            ),
            grouped[key],
        )
        if index + 1 < len(order):
            print("      ")
    return True


def emit_spot_tables(rows):
    snapshot_rows = [
        row
        for row in rows
        if row.get("source") == "spotnode_snapshot" and row.get("socket")
    ]
    if not snapshot_rows:
        return False
    emitted = False
    emitted = spot_snapshot_table(
        "Auto-HWM spotnode",
        [row for row in snapshot_rows if row.get("owner") == "node"],
    ) or emitted
    emitted = spot_snapshot_table(
        "Auto-HWM spot handles",
        [row for row in snapshot_rows if row.get("owner") == "spot"],
    ) or emitted
    return emitted


def expected_hwm(row):
    unit_budget = parse_int(row.get("unit_budget_bytes", ""), 0)
    msg_unit = parse_int(row.get("effective_message_bytes", ""), 0)
    size_cap = parse_int(row.get("size_cap", ""), 0)
    if unit_budget <= 0 or msg_unit <= 0:
        return None
    hwm = (unit_budget + msg_unit - 1) // msg_unit
    hwm = max(1, hwm)
    if size_cap > 0:
        hwm = min(hwm, size_cap)
    return hwm


def expected_match_score(row):
    expected = expected_hwm(row)
    if expected is None:
        return 2
    sndhwm = parse_int(row.get("sndhwm", ""), -1)
    rcvhwm = parse_int(row.get("rcvhwm", ""), -1)
    visible = 0
    matches = 0
    if sndhwm >= 0:
        visible += 1
        if sndhwm == expected:
            matches += 1
    if rcvhwm >= 0:
        visible += 1
        if rcvhwm == expected:
            matches += 1
    if visible == 0:
        return 2
    return 0 if matches == visible else 1 if matches > 0 else 2


def select_non_spot_rows(rows):
    selected = {}
    for index, row in enumerate(rows):
        key = (
            row.get("msg_size", ""),
            row.get("component", ""),
            row.get("socket_type", ""),
            row.get("unit_budget_bytes", ""),
            row.get("effective_message_bytes", ""),
        )
        score = expected_match_score(row)
        previous = selected.get(key)
        if previous is None or score < previous[0] or (
            score == previous[0] and index > previous[1]
        ):
            selected[key] = (score, index, row)
    return [item[2] for item in selected.values()]


pattern = normalize_pattern(sys.argv[1])
seen = set()
rows = []
for raw_path in sys.argv[2:]:
    path = pathlib.Path(raw_path)
    if not path.exists():
        continue
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = parse_detail_line(line)
        if not fields or normalize_pattern(fields.get("pattern", "")) != pattern:
            continue
        key = (
            fields.get("pattern", ""),
            fields.get("transport", ""),
            fields.get("component", ""),
            fields.get("label", ""),
            fields.get("msg_size", ""),
            fields.get("source", ""),
            fields.get("role", ""),
            fields.get("scope", ""),
            fields.get("sndhwm", ""),
            fields.get("rcvhwm", ""),
            fields.get("effective_message_bytes", ""),
            fields.get("effective_sndbuf", ""),
            fields.get("effective_rcvbuf", ""),
            fields.get("socket_message_slots", ""),
            fields.get("unit_budget_bytes", ""),
        )
        if key in seen:
            continue
        seen.add(key)
        rows.append(fields)

if not rows:
    raise SystemExit(0)

if pattern in SPOT_CONTROL_PATTERNS:
    emit_spot_tables(rows)
    raise SystemExit(0)

rows = [
    row for row in rows
    if row.get("msg_size", "") and row.get("msg_size", "") != "0"
]
if not rows:
    raise SystemExit(0)
rows.sort(
    key=lambda row: (
        parse_int(row.get("msg_size", ""), 0),
        row.get("component", ""),
        row.get("socket_type", ""),
    )
)
display_rows = []
seen_display = set()
for fields in select_non_spot_rows(rows):
    display = dict(fields)
    display["type"] = fields.get("socket_type", "")
    msg_size = fields.get("msg_size", "")
    display["msg_size_display"] = msg_size if msg_size and msg_size != "0" else "?"
    display["unit_budget_kb"] = bytes_to_kb(fields.get("unit_budget_bytes", ""))
    display["effective_sndbuf_kb"] = bytes_to_kb(fields.get("effective_sndbuf", ""))
    display["effective_rcvbuf_kb"] = bytes_to_kb(fields.get("effective_rcvbuf", ""))
    key = tuple(
        display.get(name, "")
        for name in (
            "msg_size_display",
            "component",
            "type",
            "unit_budget_kb",
            "effective_message_bytes",
            "sndhwm",
            "rcvhwm",
            "effective_sndbuf_kb",
            "effective_rcvbuf_kb",
        )
    )
    if key in seen_display:
        continue
    seen_display.add(key)
    display_rows.append(display)
if not display_rows:
    raise SystemExit(0)
print("    Auto-HWM detail:")
emit_markdown_table(
    "      ",
    (
        ("Size(B)", "msg_size_display"),
        ("Component", "component"),
        ("Type", "type"),
        ("UnitBudget(KB)", "unit_budget_kb"),
        ("MsgUnit(B)", "effective_message_bytes"),
        ("SNDHWM", "sndhwm"),
        ("RCVHWM", "rcvhwm"),
        ("SNDBUF(KB)", "effective_sndbuf_kb"),
        ("RCVBUF(KB)", "effective_rcvbuf_kb"),
    ),
    display_rows,
)
PY
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern)
      require_arg "$1" "${2:-}"
      PATTERN="${2:-}"
      shift
      ;;
    --transports)
      require_arg "$1" "${2:-}"
      TRANSPORTS="${2:-}"
      shift
      ;;
    --msg-sizes)
      require_arg "$1" "${2:-}"
      MSG_SIZES="${2:-}"
      shift
      ;;
    --clients)
      require_arg "$1" "${2:-}"
      CLIENTS="${2:-}"
      shift
      ;;
    --build-dir)
      require_arg "$1" "${2:-}"
      BUILD_DIR="${2:-}"
      shift
      ;;
    --reuse-build)
      REUSE_BUILD=1
      ;;
    --clean-build)
      CLEAN_BUILD=1
      ;;
    --output)
      require_arg "$1" "${2:-}"
      OUTPUT_PATH="${2:-}"
      shift
      ;;
    --pin-cpu)
      PIN_CPU=1
      ;;
    --io-threads)
      require_arg "$1" "${2:-}"
      COMMON_IO_THREADS="${2:-}"
      shift
      ;;
    --server-io-threads)
      require_arg "$1" "${2:-}"
      SERVER_IO_THREADS="${2:-}"
      shift
      ;;
    --client-io-threads)
      require_arg "$1" "${2:-}"
      CLIENT_IO_THREADS="${2:-}"
      shift
      ;;
    --hwm)
      require_arg "$1" "${2:-}"
      HWM="${2:-}"
      shift
      ;;
    --send-hwm)
      require_arg "$1" "${2:-}"
      SNDHWM="${2:-}"
      shift
      ;;
    --recv-hwm)
      require_arg "$1" "${2:-}"
      RCVHWM="${2:-}"
      shift
      ;;
    --buf)
      require_arg "$1" "${2:-}"
      SNDBUF="${2:-}"
      RCVBUF="${2:-}"
      shift
      ;;
    --sndbuf)
      require_arg "$1" "${2:-}"
      SNDBUF="${2:-}"
      shift
      ;;
    --rcvbuf)
      require_arg "$1" "${2:-}"
      RCVBUF="${2:-}"
      shift
      ;;
    --sndtimeo|--send-timeout-ms)
      require_arg "$1" "${2:-}"
      SNDTIMEO_MS="${2:-}"
      shift
      ;;
    --rcvtimeo|--recv-timeout-ms)
      require_arg "$1" "${2:-}"
      RCVTIMEO_MS="${2:-}"
      shift
      ;;
    --connect-concurrency)
      require_arg "$1" "${2:-}"
      CONNECT_CONCURRENCY="${2:-}"
      shift
      ;;
    --transport-transition-ms)
      require_arg "$1" "${2:-}"
      TRANSPORT_TRANSITION_MS="${2:-}"
      shift
      ;;
    --pattern-transition-ms)
      require_arg "$1" "${2:-}"
      PATTERN_TRANSITION_MS="${2:-}"
      shift
      ;;
    --server-ready-timeout-ms)
      require_arg "$1" "${2:-}"
      SERVER_READY_TIMEOUT_MS="${2:-}"
      shift
      ;;
    --connect-ready-timeout-ms)
      require_arg "$1" "${2:-}"
      READY_TIMEOUT_MS="${2:-}"
      shift
      ;;
    --monitor-hwm)
      require_arg "$1" "${2:-}"
      MONITOR_HWM="${2:-}"
      shift
      ;;
    --server-shutdown-timeout-ms)
      require_arg "$1" "${2:-}"
      SERVER_SHUTDOWN_TIMEOUT_MS="${2:-}"
      shift
      ;;
    --server-bind-port)
      require_arg "$1" "${2:-}"
      SERVER_BIND_PORT="${2:-}"
      shift
      ;;
    --auto-hwm-profile)
      require_arg "$1" "${2:-}"
      CTX_AUTO_HWM_PROFILE="${2:-}"
      shift
      ;;
    --duration)
      require_arg "$1" "${2:-}"
      DURATION="${2:-}"
      shift
      ;;
    --runs)
      require_arg "$1" "${2:-}"
      RUNS="${2:-}"
      shift
      ;;
    --runs=*)
      RUNS="${1#--runs=}"
      ;;
    --results-dir)
      require_arg "$1" "${2:-}"
      RESULTS_ROOT="${2:-}"
      shift
      ;;
    --results-tag)
      require_arg "$1" "${2:-}"
      RESULTS_TAG="${2:-}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [[ "${REUSE_BUILD}" -eq 1 && "${CLEAN_BUILD}" -eq 1 ]]; then
  echo "--reuse-build and --clean-build are mutually exclusive." >&2
  exit 1
fi

validate_uint "--duration" "${DURATION}"
validate_uint "--runs" "${RUNS}"
validate_uint "PERF_MULTI_CONNECT_READY_TIMEOUT_MS" "${READY_TIMEOUT_MS}"
validate_nonnegative_uint "PERF_MULTI_SERVER_READY_TIMEOUT_MS" "${SERVER_READY_TIMEOUT_MS}"
validate_nonnegative_uint "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS" "${SERVER_SHUTDOWN_TIMEOUT_MS}"
validate_uint "PERF_MULTI_TIMEOUT_SECONDS" "${RESULT_TIMEOUT_SECONDS}"
validate_nonnegative_uint "PERF_MULTI_CASE_COOLDOWN_MS" "${CASE_COOLDOWN_MS}"
validate_nonnegative_uint "PERF_MULTI_TRANSPORT_TRANSITION_MS" "${TRANSPORT_TRANSITION_MS}"
validate_nonnegative_uint "PERF_MULTI_PATTERN_TRANSITION_MS" "${PATTERN_TRANSITION_MS}"
validate_nonnegative_uint "PERF_MULTI_MONITOR_HWM" "${MONITOR_HWM}"
validate_nonnegative_uint "PERF_MULTI_SERVER_BIND_PORT" "${SERVER_BIND_PORT}"
if (( SERVER_BIND_PORT > 65535 )); then
  echo "PERF_MULTI_SERVER_BIND_PORT must be in range 0..65535." >&2
  exit 1
fi
validate_uint "PERF_MULTI_SNDTIMEO_MS" "${SNDTIMEO_MS}"
validate_uint "PERF_MULTI_RCVTIMEO_MS" "${RCVTIMEO_MS}"
validate_byte_size_token "PERF_MULTI_SNDBUF" "${SNDBUF}"
validate_byte_size_token "PERF_MULTI_RCVBUF" "${RCVBUF}"
case "${CTX_AUTO_HWM_PROFILE}" in
  ""|compact|low_latency|low-latency|balanced|throughput) ;;
  *)
    echo "PERF_CTX_AUTO_HWM_PROFILE must be compact, low_latency, balanced, or throughput." >&2
    exit 1
    ;;
esac
case "${CTX_AUTO_HWM_ENABLE}" in
  0|1) ;;
  *)
    echo "PERF_CTX_AUTO_HWM_ENABLE must be 0 or 1." >&2
    exit 1
    ;;
esac
if [[ -n "${COMMON_IO_THREADS}" ]]; then
  validate_uint "--io-threads" "${COMMON_IO_THREADS}"
fi
if [[ -n "${SERVER_IO_THREADS}" ]]; then
  validate_uint "--server-io-threads" "${SERVER_IO_THREADS}"
fi
if [[ -n "${CLIENT_IO_THREADS}" ]]; then
  validate_uint "--client-io-threads" "${CLIENT_IO_THREADS}"
fi
if [[ -n "${CONNECT_CONCURRENCY}" ]]; then
  validate_uint "--connect-concurrency" "${CONNECT_CONCURRENCY}"
fi
if [[ -n "${HWM}" ]]; then
  validate_uint "--hwm" "${HWM}"
fi
if [[ -n "${SNDHWM}" ]]; then
  validate_uint "--send-hwm" "${SNDHWM}"
fi
if [[ -n "${RCVHWM}" ]]; then
  validate_uint "--recv-hwm" "${RCVHWM}"
fi
if [[ -n "${HWM}${SNDHWM}${RCVHWM}${SNDBUF}${RCVBUF}" && "${ALLOW_MANUAL_SOCKET_OVERRIDES}" != "1" ]]; then
  echo "Error: manual HWM/SNDBUF/RCVBUF overrides are debug-only." >&2
  echo "Set PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1 to use --hwm/--send-hwm/--recv-hwm/--buf/--sndbuf/--rcvbuf." >&2
  exit 1
fi

if [[ -n "${CLIENTS}" ]]; then
  validate_uint "--clients" "${CLIENTS}"
fi

if [[ -n "${MSG_SIZES}" && ! "${MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "--msg-sizes must be a comma-separated list of positive integers." >&2
  exit 1
fi

if [[ ! "${TRANSPORTS}" =~ ^[a-z]+(,[a-z]+)*$ ]]; then
  echo "--transports must be a comma-separated list of transport names." >&2
  exit 1
fi

PATTERN="$(normalize_multi_pattern_csv "${PATTERN}")"
EFFECTIVE_MSG_SIZES_DISPLAY="$(effective_msg_sizes_display "${PATTERN}" "${MSG_SIZES}")"
EFFECTIVE_CLIENTS_DISPLAY="$(effective_clients_display "${PATTERN}" "${CLIENTS}")"
if [[ -n "${SERVER_IO_THREADS}" ]]; then
  display_server_io_threads="${SERVER_IO_THREADS}"
elif [[ -n "${COMMON_IO_THREADS}" ]]; then
  display_server_io_threads="${COMMON_IO_THREADS} (from PERF_IO_THREADS)"
else
  display_server_io_threads="4 (default)"
fi
if [[ -n "${CLIENT_IO_THREADS}" ]]; then
  display_client_io_threads="${CLIENT_IO_THREADS}"
elif [[ -n "${COMMON_IO_THREADS}" ]]; then
  display_client_io_threads="${COMMON_IO_THREADS} (from PERF_IO_THREADS)"
else
  display_client_io_threads="4 (default)"
fi
if [[ "${ALLOW_MANUAL_SOCKET_OVERRIDES}" == "1" ]]; then
  DISPLAY_HWM="${HWM:-auto-hwm}"
  DISPLAY_SNDHWM="${SNDHWM:-${HWM:-auto-hwm}}"
  DISPLAY_RCVHWM="${RCVHWM:-${HWM:-auto-hwm}}"
  DISPLAY_SNDBUF="${SNDBUF:-auto-hwm}"
  DISPLAY_RCVBUF="${RCVBUF:-auto-hwm}"
else
  DISPLAY_HWM="auto-hwm"
  DISPLAY_SNDHWM="auto-hwm"
  DISPLAY_RCVHWM="auto-hwm"
  DISPLAY_SNDBUF="auto-hwm"
  DISPLAY_RCVBUF="auto-hwm"
fi
display_connect_concurrency="${CONNECT_CONCURRENCY:-}"
if [[ -z "${display_connect_concurrency}" ]]; then
  if [[ "${EFFECTIVE_CLIENTS_DISPLAY}" =~ ^[0-9]+$ && "${EFFECTIVE_CLIENTS_DISPLAY}" -ge 10000 ]]; then
    display_connect_concurrency="1024 (default)"
  else
    display_connect_concurrency="128 (default)"
  fi
fi
display_pin_cpu="off"
if [[ "${PIN_CPU}" -eq 1 ]]; then
  display_pin_cpu="on"
fi
mkdir -p "${RESULTS_ROOT}/multi/tmp" "${RESULTS_ROOT}/multi/report"

platform="$(normalize_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report_base="perf_dotnet_multi_${platform}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report_base="${report_base}_${RESULTS_TAG}"
fi
REPORT="${RESULTS_ROOT}/multi/report/${report_base}.txt"
: > "${REPORT}"
if [[ -n "${OUTPUT_PATH}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_PATH}")"
  : > "${OUTPUT_PATH}"
fi
prune_report_dir "${RESULTS_ROOT}/multi/report" \
  "${PERF_RESULTS_MAX_FILES:-100}"
FAILURES_FILE="${RESULTS_ROOT}/multi/tmp/${report_base}.failures.csv"
RESULT_DATA_FILE="${RESULTS_ROOT}/multi/tmp/${report_base}.result_data.csv"
: > "${FAILURES_FILE}"
: > "${RESULT_DATA_FILE}"

record_failure() {
  local pattern="${1:-}"
  local transport="${2:-}"
  local size="${3:-}"
  local run_index="${4:-}"
  local reason="${5:-}"
  printf '%s,%s,%s,%s,%s\n' \
    "${pattern}" "${transport}" "${size}" "${run_index}" "${reason}" >> "${FAILURES_FILE}"
  failure_count=$(( ${failure_count:-0} + 1 ))
  print_line "    Testing ${transport} | ${size}B:"
  emit_failure_row "${size}"
  if [[ "${PERF_FAIL_FAST:-0}" == "1" ]]; then
    print_line "    Testing ${transport}: Done"
    print_failures_section "${FAILURES_FILE}"
    SHOW_TOTAL_TIME=1
    exit 1
  fi
}

run_multi_process() {
  local role="$1"
  local log_path="$2"
  local endpoint="${3:-}"
  local control_fd="${4:-}"
  local background="${5:-0}"
  shift 5
  local extra_args=("$@")
  local shell_cmd="${PERF_BINARY@Q} --multi-${role} ${pattern@Q} ${transport@Q} ${size@Q}"
  local role_io_threads="${COMMON_IO_THREADS:-4}"
  if [[ "${role}" == "server" && -n "${SERVER_IO_THREADS}" ]]; then
    role_io_threads="${SERVER_IO_THREADS}"
  elif [[ "${role}" == "client" && -n "${CLIENT_IO_THREADS}" ]]; then
    role_io_threads="${CLIENT_IO_THREADS}"
  fi
  local effective_ready_timeout="${READY_TIMEOUT_MS}"
  if [[ "${pattern}" == "MULTI_SPOT" || "${pattern}" == "MULTI_SPOT_REQREP" ]]; then
    if [[ "${transport}" == "tls" || "${transport}" == "wss" ]]; then
      if (( effective_ready_timeout < 12000 )); then
        effective_ready_timeout=12000
      fi
    fi
  fi
  local normalized_pattern="${pattern#MULTI_}"
  local env_prefix=(
    "PERF_PATTERN=${normalized_pattern}"
    "PERF_MULTI_PATTERN=${normalized_pattern}"
    "PERF_MULTI_TRANSPORT=${transport}"
    "PERF_MULTI_COMPONENT=${role}"
    "PERF_DOTNET_SERVER_STATS=${PERF_DOTNET_SERVER_STATS:-0}"
    "PERF_DOTNET_TIMING=${PERF_DOTNET_TIMING:-0}"
    # Match bindings/c/perf/multi/common/perf_multi_runtime.hpp:54:
    # bench_io_threads() default = 4. .NET default was 0 (no override =>
    # zlink ctx default 1), which capped per-process to single-core
    # throughput vs C's multi-core internal IO workers.
    "PERF_IO_THREADS=${role_io_threads}"
    "PERF_MULTI_CLIENTS=${pattern_clients}"
    "PERF_MULTI_DURATION_SECONDS=${DURATION}"
    "PERF_MULTI_CONNECT_READY_TIMEOUT_MS=${effective_ready_timeout}"
    "PERF_MULTI_SERVER_READY_TIMEOUT_MS=${SERVER_READY_TIMEOUT_MS}"
    "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS=${SERVER_SHUTDOWN_TIMEOUT_MS}"
    "PERF_MULTI_SERVER_BIND_PORT=${SERVER_BIND_PORT}"
    "PERF_MULTI_SNDTIMEO_MS=${SNDTIMEO_MS}"
    "PERF_MULTI_RCVTIMEO_MS=${RCVTIMEO_MS}"
    "PERF_MULTI_MONITOR_HWM=${MONITOR_HWM}"
    "PERF_CTX_AUTO_HWM_ENABLE=${CTX_AUTO_HWM_ENABLE}"
    "PERF_CTX_AUTO_HWM_PROFILE=${CTX_AUTO_HWM_PROFILE}"
    "DOTNET_TieredCompilation=1"
    "DOTNET_TC_QuickJitForLoops=1"
    "DOTNET_ReadyToRun=1"
  )
  if [[ -n "${CONNECT_CONCURRENCY}" ]]; then
    env_prefix+=("PERF_MULTI_CONNECT_CONCURRENCY=${CONNECT_CONCURRENCY}")
  fi
  if [[ "${ALLOW_MANUAL_SOCKET_OVERRIDES}" == "1" ]]; then
    env_prefix+=("PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1")
    [[ -n "${HWM}" ]] && env_prefix+=("PERF_MULTI_HWM=${HWM}")
    [[ -n "${SNDHWM}" ]] && env_prefix+=("PERF_MULTI_SNDHWM=${SNDHWM}")
    [[ -n "${RCVHWM}" ]] && env_prefix+=("PERF_MULTI_RCVHWM=${RCVHWM}")
    [[ -n "${SNDBUF}" ]] && env_prefix+=("PERF_MULTI_SNDBUF=${SNDBUF}")
    [[ -n "${RCVBUF}" ]] && env_prefix+=("PERF_MULTI_RCVBUF=${RCVBUF}")
  fi

  if [[ -n "${endpoint}" ]]; then
    shell_cmd+=" --endpoint ${endpoint@Q}"
  fi

  local extra_arg
  for extra_arg in "${extra_args[@]}"; do
    shell_cmd+=" ${extra_arg@Q}"
  done

  if [[ "${PIN_CPU}" -eq 1 && "$(uname -s)" == Linux* ]] \
    && command -v taskset >/dev/null 2>&1; then
    shell_cmd="taskset -c 1 ${shell_cmd}"
  fi

  if [[ "${background}" == "1" ]]; then
    if [[ -n "${control_fd}" ]]; then
      if [[ "${control_fd}" =~ ^[0-9]+$ ]]; then
        env "${env_prefix[@]}" bash -lc "${shell_cmd}" <&${control_fd} > "${log_path}" 2>&1 &
      else
        env "${env_prefix[@]}" bash -lc "${shell_cmd}" < "${control_fd}" > "${log_path}" 2>&1 &
      fi
    else
      env "${env_prefix[@]}" bash -lc "${shell_cmd}" > "${log_path}" 2>&1 &
    fi
    return 0
  fi

  if [[ -n "${control_fd}" ]]; then
    env "${env_prefix[@]}" bash -lc "${shell_cmd}" <&${control_fd} > "${log_path}" 2>&1
  else
    if command -v timeout >/dev/null 2>&1; then
      env "${env_prefix[@]}" timeout "${RESULT_TIMEOUT_SECONDS}s" \
        bash -lc "${shell_cmd}" > "${log_path}" 2>&1
    else
      env "${env_prefix[@]}" bash -lc "${shell_cmd}" > "${log_path}" 2>&1
    fi
  fi
}

run_external_stream_client() {
  local endpoint="$1"
  ensure_stream_client
  local cmd=(
    "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM
    --sizes "${size}" --runs 1 --duration "${DURATION}"
    --ccu "${pattern_clients}" --send-stop-token 1 --endpoint "${endpoint}"
  )
  if [[ "${PIN_CPU}" -eq 1 && "$(uname -s)" == Linux* ]] \
    && command -v taskset >/dev/null 2>&1; then
    cmd=(taskset -c 1 "${cmd[@]}")
  fi
  env \
    "PERF_PATTERN=${pattern#MULTI_}" \
    "PERF_MULTI_PATTERN=${pattern#MULTI_}" \
    "PERF_MULTI_TRANSPORT=${transport}" \
    "PERF_MULTI_COMPONENT=client" \
    "${cmd[@]}" > "${client_log}" 2>&1
}

ensure_build_output
prepare_core_runtime

if ! PERF_BINARY="$(resolve_perf_binary "${PROJECT_DIR}" "Zlink.BindingBench.Multi")"; then
  echo "multi benchmark binary not found under ${PROJECT_DIR}/bin/${CONFIGURATION}/net8.0." >&2
  echo "Gate 1 build output is required before smoke." >&2
  exit 1
fi

print_line "## Effective Options (start)"
print_line "- lang: dotnet"
print_line "- suite: multi"
print_line "- runs: ${RUNS}"
print_line "- duration_seconds: ${DURATION}"
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS}"
print_line "- msg_sizes: ${EFFECTIVE_MSG_SIZES_DISPLAY}"
print_line "- clients: ${EFFECTIVE_CLIENTS_DISPLAY}"
print_line "- default_clients: 100"
print_line "- default_stream_clients: 10000"
print_line "- service_clients: auto"
print_line "- server_io_threads: ${display_server_io_threads}"
print_line "- client_io_threads: ${display_client_io_threads}"
print_line "- hwm: ${DISPLAY_HWM}"
print_line "- sndhwm: ${DISPLAY_SNDHWM}"
print_line "- rcvhwm: ${DISPLAY_RCVHWM}"
print_line "- sndbuf: ${DISPLAY_SNDBUF}"
print_line "- rcvbuf: ${DISPLAY_RCVBUF}"
print_line "- ctx_auto_hwm_enable: ${CTX_AUTO_HWM_ENABLE}"
print_line "- ctx_auto_hwm_profile: ${CTX_AUTO_HWM_PROFILE}"
print_line "- sndtimeo_ms: ${SNDTIMEO_MS}"
print_line "- rcvtimeo_ms: ${RCVTIMEO_MS}"
print_line "- connect_concurrency: ${display_connect_concurrency}"
print_line "- connect_ready_timeout_ms: ${READY_TIMEOUT_MS}"
print_line "- monitor_hwm: ${MONITOR_HWM}"
print_line "- server_ready_timeout_ms: ${SERVER_READY_TIMEOUT_MS}"
print_line "- server_shutdown_timeout_ms: ${SERVER_SHUTDOWN_TIMEOUT_MS}"
print_line "- server_bind_port: ${SERVER_BIND_PORT}"
print_line "- transport_transition_ms: ${TRANSPORT_TRANSITION_MS}"
print_line "- pattern_transition_ms: ${PATTERN_TRANSITION_MS}"
print_line "- lat_timeout_ms: ${PERF_LAT_TIMEOUT_MS:-5000}"
print_line "- stream_non_tcp_clients_max: ${PERF_STREAM_NON_TCP_CLIENTS_MAX:-10000}"
print_line "- disable_resource_metrics: ${PERF_DISABLE_RESOURCE_METRICS:-0}"
print_line "- timeout_seconds: ${TIMEOUT_SECONDS_DISPLAY}"
print_line "- runtime: ${CORE_LIB}"
print_line "- pin_cpu: ${display_pin_cpu}"
print_line ""

IFS=',' read -r -a patterns <<< "${PATTERN}"
IFS=',' read -r -a transports <<< "${TRANSPORTS}"

status=0
result_lines=0
expected_result_lines=0
failure_count=0
success_count=0
unsupported_count=0
skip_count=0
for (( run_index=1; run_index<=RUNS; run_index++ )); do
  for pattern_index in "${!patterns[@]}"; do
    pattern="${patterns[pattern_index]}"
    pattern="${pattern//[[:space:]]/}"
    [[ -n "${pattern}" ]] || continue

    pattern_msg_sizes="${MSG_SIZES}"
    if [[ -z "${pattern_msg_sizes}" ]]; then
      pattern_msg_sizes="$(default_msg_sizes_for_pattern "${pattern}")"
    fi
    pattern_clients="${CLIENTS}"
    if [[ -z "${pattern_clients}" ]]; then
      pattern_clients="$(default_clients_for_pattern "${pattern}")"
    fi

    IFS=',' read -r -a msg_sizes <<< "${pattern_msg_sizes}"
    pattern_kind="one-way"
    case "${pattern}" in
      MULTI_DEALER_ROUTER|MULTI_ROUTER_ROUTER|MULTI_STREAM|MULTI_SPOT_REQREP|MULTI_SPOT_SENDSEND)
        pattern_kind="echo"
        ;;
    esac
    print_line ""
    print_line "==============================================================================="
    print_line ""
    print_line "## PATTERN: ${pattern} (${pattern_kind})"
    print_line "  > Benchmarking current for ${pattern}..."
    pattern_auto_hwm_logs=()

    for transport_index in "${!transports[@]}"; do
      transport="${transports[transport_index]}"
      transport="${transport//[[:space:]]/}"
      [[ -n "${transport}" ]] || continue
      if [[ "${transport}" == "inproc" ]]; then
        print_line "UNSUPPORTED,dotnet,${pattern},${transport}"
        unsupported_count=$((unsupported_count + 1))
        continue
      fi

      print_line "    Testing ${transport}:"
      print_line "      | Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |"
      print_line "      |----------|--------------------|----------------|---------------|---------------|---------------|"

      for size in "${msg_sizes[@]}"; do
        size="${size//[[:space:]]/}"
        [[ -n "${size}" ]] || continue
        if [[ "${CASE_COOLDOWN_MS}" -gt 0 ]]; then
          sleep "$(( (CASE_COOLDOWN_MS + 999) / 1000 ))"
        fi
        expected_result_lines=$((expected_result_lines + 5))

        metrics_file="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_run${run_index}.metrics"
        : > "${metrics_file}"
        server_log="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_server_run${run_index}.log"
        client_log="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_client_run${run_index}.log"
        server_control_fifo="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_server_run${run_index}.ctl"
        client_control_fifo="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_client_run${run_index}.ctl"
        rm -f "${server_log}" "${client_log}" \
          "${server_control_fifo}" "${client_control_fifo}"

        server_control_fd=''
        client_control_fd=''
        server_endpoint=''
        server_pid=0
        server_started=0
        for _srv_attempt in 1 2 3; do
          [[ "${_srv_attempt}" -gt 1 ]] && sleep 2
          rm -f "${server_log}"
          if [[ -n "${server_control_fd}" ]]; then
            exec {server_control_fd}>&-
            server_control_fd=''
          fi
          if pattern_uses_control_pipe "${pattern}"; then
            mkfifo "${server_control_fifo}"
            run_multi_process "server" "${server_log}" "" "${server_control_fifo}" 1
            server_pid=$!
            exec {server_control_fd}>"${server_control_fifo}"
            rm -f "${server_control_fifo}"
          else
            run_multi_process "server" "${server_log}" "" "" 1
            server_pid=$!
          fi
          if server_endpoint="$(wait_for_ready_endpoint "${server_log}" "${SERVER_READY_TIMEOUT_MS}")"; then
            server_started=1
            break
          fi
          terminate_pid "${server_pid}"
          is_eaddrinuse_log "${server_log}" || break
        done

        if [[ "${server_started}" -ne 1 ]]; then
          if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${server_log}" 2>/dev/null)"; then
            print_line "${unsupported_line}"
            unsupported_count=$((unsupported_count + 1))
            expected_result_lines=$((expected_result_lines - 5))
            if [[ -n "${server_control_fd}" ]]; then
              exec {server_control_fd}>&-
            fi
            continue
          fi
          cat "${server_log}" >&2 || true
          echo "server did not become ready for ${pattern} ${transport} ${size}" >&2
          record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "server_ready_timeout"
          if [[ -n "${server_control_fd}" ]]; then
            exec {server_control_fd}>&-
          fi
          status=1
          continue
        fi

        if [[ "${pattern}" == "MULTI_STREAM" ]]; then
          if run_external_stream_client "${server_endpoint}"; then
            write_control_line "${server_control_fd}" 'STOP\n'
            if ! wait_for_pid "${server_pid}" "$(shutdown_timeout_seconds)"; then
              terminate_pid "${server_pid}"
            else
              wait "${server_pid}" || true
            fi
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
              print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              exec {server_control_fd}>&-
              continue
            fi
            if ! extracted="$(extract_results_from_logs "${client_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"; then
              record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "missing_required_result_lines"
              status=1
              exec {server_control_fd}>&-
              continue
            fi
          else
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
              print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              write_control_line "${server_control_fd}" 'STOP\n'
              terminate_pid "${server_pid}"
              exec {server_control_fd}>&-
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "process_exit_nonzero"
            status=1
            exec {server_control_fd}>&-
            continue
          fi
          exec {server_control_fd}>&-
        elif [[ "${pattern}" == "MULTI_SPOT" || "${pattern}" == "MULTI_SPOT_REQREP" || "${pattern}" == "MULTI_SPOT_SENDSEND" ]]; then
          control_endpoint=''
          if ! control_endpoint="$(wait_for_control_ready_endpoint "${server_log}" "${SERVER_READY_TIMEOUT_MS}")"; then
            cat "${server_log}" >&2 || true
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "server_ready_timeout"
            exec {server_control_fd}>&-
            status=1
            continue
          fi

          mkfifo "${client_control_fifo}"
          run_multi_process "client" "${client_log}" "${server_endpoint}" "${client_control_fifo}" 1 "--control-endpoint" "${control_endpoint}"
          client_pid=$!
          exec {client_control_fd}>"${client_control_fifo}"
          rm -f "${client_control_fifo}"

          client_ctrl_ep=''
          if ! client_ctrl_ep="$(wait_for_client_control_endpoint "${client_log}" "${READY_TIMEOUT_MS}")"; then
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
              print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              terminate_pid "${client_pid}"
              write_control_line "${server_control_fd}" 'STOP\n'
              terminate_pid "${server_pid}"
              exec {server_control_fd}>&-
              exec {client_control_fd}>&-
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${client_pid}"
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "client_control_timeout"
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            status=1
            continue
          fi

          write_control_line "${server_control_fd}" 'CONNECT_CONTROL,%s\n' "${client_ctrl_ep}"

          connected_ep=''
          if ! connected_ep="$(wait_for_control_connected "${server_log}" "${READY_TIMEOUT_MS}")"; then
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${client_pid}"
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "control_connect_timeout"
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            status=1
            continue
          fi

          write_control_line "${client_control_fd}" 'CONTROL_CONNECTED,%s\n' "${connected_ep}"

          if ! wait_for_client_ready_line "${client_log}" "${READY_TIMEOUT_MS}"; then
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
             print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              terminate_pid "${client_pid}"
              write_control_line "${server_control_fd}" 'STOP\n'
              terminate_pid "${server_pid}"
              exec {server_control_fd}>&-
              exec {client_control_fd}>&-
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${client_pid}"
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "client_ready_timeout"
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            status=1
            continue
          fi

          write_control_line "${server_control_fd}" 'START,%s\n' "${size}"
          write_control_line "${client_control_fd}" 'START,%s\n' "${size}"
          if ! wait_for_result_line "${client_log}" \
            "RESULT,dotnet,${pattern#MULTI_},${transport},${size},latency_p99," \
            "${RESULT_TIMEOUT_SECONDS}"; then
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
              print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              write_control_line "${server_control_fd}" 'STOP\n'
              terminate_pid "${client_pid}"
              terminate_pid "${server_pid}"
              exec {server_control_fd}>&-
              exec {client_control_fd}>&-
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${client_pid}"
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "result_timeout"
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            status=1
            continue
          fi

          if ! wait_for_pid "${client_pid}" "$(shutdown_timeout_seconds)"; then
            terminate_pid "${client_pid}"
          else
            wait "${client_pid}" || true
          fi
          if ! wait_for_pid "${server_pid}" "$(shutdown_timeout_seconds)"; then
            terminate_pid "${server_pid}"
          else
            wait "${server_pid}" || true
          fi
          if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
             print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            continue
          fi
          if ! extracted="$(extract_results_from_logs "${client_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"; then
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "missing_required_result_lines"
            status=1
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            continue
          fi
          exec {server_control_fd}>&-
          exec {client_control_fd}>&-
        elif [[ "${pattern}" == "MULTI_DEALER_DEALER" || "${pattern}" == "MULTI_PUBSUB" ]]; then
          mkfifo "${client_control_fifo}"
          run_multi_process "client" "${client_log}" "${server_endpoint}" "${client_control_fifo}" 1
          client_pid=$!
          exec {client_control_fd}>"${client_control_fifo}"
          rm -f "${client_control_fifo}"

          if ! wait_for_client_ready_line "${client_log}" "${READY_TIMEOUT_MS}"; then
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
             print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              terminate_pid "${client_pid}"
              write_control_line "${server_control_fd}" 'STOP\n'
              terminate_pid "${server_pid}"
              exec {server_control_fd}>&-
              exec {client_control_fd}>&-
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${client_pid}"
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "client_ready_timeout"
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            status=1
            continue
          fi

          write_control_line "${server_control_fd}" 'START,%s\n' "${size}"
          write_control_line "${client_control_fd}" 'START,%s\n' "${size}"

          if ! wait_for_result_line "${client_log}" \
            "RESULT,dotnet,${pattern#MULTI_},${transport},${size},latency_p99," \
            "${RESULT_TIMEOUT_SECONDS}"; then
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
             print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
              write_control_line "${server_control_fd}" 'STOP\n'
              terminate_pid "${client_pid}"
              terminate_pid "${server_pid}"
              exec {server_control_fd}>&-
              exec {client_control_fd}>&-
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            write_control_line "${server_control_fd}" 'STOP\n'
            terminate_pid "${client_pid}"
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "result_timeout"
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            status=1
            continue
          fi

          write_control_line "${server_control_fd}" 'STOP\n'
          write_control_line "${client_control_fd}" 'STOP\n'
          if ! wait_for_pid "${client_pid}" "$(shutdown_timeout_seconds)"; then
            terminate_pid "${client_pid}"
          else
            wait "${client_pid}" || true
          fi
          if ! wait_for_pid "${server_pid}" "$(shutdown_timeout_seconds)"; then
            terminate_pid "${server_pid}"
          else
            wait "${server_pid}" || true
          fi
          if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
             print_line "${unsupported_line}"
              unsupported_count=$((unsupported_count + 1))
              expected_result_lines=$((expected_result_lines - 5))
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            continue
          fi
          if ! extracted="$(extract_results_from_logs "${client_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"; then
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "missing_required_result_lines"
            status=1
            exec {server_control_fd}>&-
            exec {client_control_fd}>&-
            continue
          fi
          exec {server_control_fd}>&-
          exec {client_control_fd}>&-
        else
          if run_multi_process "client" "${client_log}" "${server_endpoint}" "" 0; then
            if ! wait_for_pid "${server_pid}" "$(shutdown_timeout_seconds)"; then
              terminate_pid "${server_pid}"
            else
              wait "${server_pid}" || true
            fi
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
           print_line "${unsupported_line}"
            unsupported_count=$((unsupported_count + 1))
            expected_result_lines=$((expected_result_lines - 5))
              continue
            fi
            if ! extracted="$(extract_results_from_logs "${client_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"; then
              record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "missing_required_result_lines"
              status=1
              continue
            fi
          else
            if unsupported_line="$(extract_unsupported_line "${pattern}" "${transport}" "${client_log}" "${server_log}" 2>/dev/null)"; then
          print_line "${unsupported_line}"
          unsupported_count=$((unsupported_count + 1))
          expected_result_lines=$((expected_result_lines - 5))
              terminate_pid "${server_pid}"
              continue
            fi
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${server_pid}"
            record_failure "${pattern}" "${transport}" "${size}" "${run_index}" "process_exit_nonzero"
            status=1
            continue
          fi
        fi

        pattern_auto_hwm_logs+=("${server_log}" "${client_log}")
        while IFS= read -r result_line; do
          [[ -n "${result_line}" ]] || continue
          printf '%s\n' "${result_line}" >> "${metrics_file}"
          printf '%s\n' "${result_line}" >> "${RESULT_DATA_FILE}"
          result_lines=$((result_lines + 1))
        done <<< "${extracted}"
        print_line "    Testing ${transport} | ${size}B:"
        while IFS= read -r table_line; do
          print_line "${table_line}"
        done < <(emit_result_row "${metrics_file}" "${pattern}")
        success_count=$((success_count + 1))
      done

      print_line "    Testing ${transport}: Done"
      if (( transport_index + 1 < ${#transports[@]} && TRANSPORT_TRANSITION_MS > 0 )); then
        print_line "    [transport cooldown ${TRANSPORT_TRANSITION_MS}ms]"
        sleep_ms "${TRANSPORT_TRANSITION_MS}"
      fi
    done

    while IFS= read -r auto_hwm_line; do
      [[ -n "${auto_hwm_line}" ]] || continue
      print_line "${auto_hwm_line}"
    done < <(emit_auto_hwm_detail_table "${pattern}" "${pattern_auto_hwm_logs[@]}")

    if (( pattern_index + 1 < ${#patterns[@]} && PATTERN_TRANSITION_MS > 0 )); then
      print_line "[pattern cooldown ${PATTERN_TRANSITION_MS}ms]"
      sleep_ms "${PATTERN_TRANSITION_MS}"
    fi
  done
done

print_line ""
print_line "## Effective Options (result)"
print_line "- lang: dotnet"
print_line "- suite: multi"
print_line "- runs: ${RUNS}"
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS}"
print_line "- msg_sizes: ${EFFECTIVE_MSG_SIZES_DISPLAY}"
print_line "- clients: ${EFFECTIVE_CLIENTS_DISPLAY}"
print_line "- duration_seconds: ${DURATION}"
print_line "- pin_cpu: ${display_pin_cpu}"
print_line "- default_clients: 100"
print_line "- default_stream_clients: 10000"
print_line "- service_clients: auto"
print_line "- server_io_threads: ${display_server_io_threads}"
print_line "- client_io_threads: ${display_client_io_threads}"
print_line "- hwm: ${DISPLAY_HWM}"
print_line "- sndhwm: ${DISPLAY_SNDHWM}"
print_line "- rcvhwm: ${DISPLAY_RCVHWM}"
print_line "- sndbuf: ${DISPLAY_SNDBUF}"
print_line "- rcvbuf: ${DISPLAY_RCVBUF}"
print_line "- ctx_auto_hwm_enable: ${CTX_AUTO_HWM_ENABLE}"
print_line "- ctx_auto_hwm_profile: ${CTX_AUTO_HWM_PROFILE}"
print_line "- sndtimeo_ms: ${SNDTIMEO_MS}"
print_line "- rcvtimeo_ms: ${RCVTIMEO_MS}"
print_line "- connect_concurrency: ${display_connect_concurrency}"
print_line "- connect_ready_timeout_ms: ${READY_TIMEOUT_MS}"
print_line "- monitor_hwm: ${MONITOR_HWM}"
print_line "- server_ready_timeout_ms: ${SERVER_READY_TIMEOUT_MS}"
print_line "- server_shutdown_timeout_ms: ${SERVER_SHUTDOWN_TIMEOUT_MS}"
print_line "- server_bind_port: ${SERVER_BIND_PORT}"
print_line "- transport_transition_ms: ${TRANSPORT_TRANSITION_MS}"
print_line "- pattern_transition_ms: ${PATTERN_TRANSITION_MS}"
print_line "- lat_timeout_ms: ${PERF_LAT_TIMEOUT_MS:-5000}"
print_line "- stream_non_tcp_clients_max: ${PERF_STREAM_NON_TCP_CLIENTS_MAX:-10000}"
print_line "- disable_resource_metrics: ${PERF_DISABLE_RESOURCE_METRICS:-0}"
print_line "- timeout_seconds: ${TIMEOUT_SECONDS_DISPLAY}"
if [[ -s "${RESULT_DATA_FILE}" ]]; then
  print_line ""
  print_line "## Result Data"
  while IFS= read -r result_line; do
    print_line "${result_line}"
  done < "${RESULT_DATA_FILE}"
fi
if [[ "${result_lines}" -eq "${expected_result_lines}" && "${status}" -eq 0 ]]; then
  completion_status="complete"
else
  completion_status="partial"
  status=1
fi
print_completion_section "${completion_status}" "${expected_result_lines}" "${result_lines}" \
  "${success_count}" "${unsupported_count}" "${skip_count}" "${failure_count}"
if [[ "${failure_count}" -gt 0 ]]; then
  print_failures_section "${FAILURES_FILE}"
fi

print_line "Saved result file: ${REPORT} (status=${completion_status})"
SHOW_TOTAL_TIME=1
exit "${status}"
