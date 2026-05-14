#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
JAVA_BINDINGS_DIR="$(cd "${ROOT_DIR}/.." && pwd)"
RESULTS_ROOT="${PERF_RESULTS_DIR:-${ROOT_DIR}/results}"
PATTERN="ALL"
TRANSPORTS=""
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
RUNS=1
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"
BUILD_DIR=""
OUTPUT_PATH=""
PIN_CPU=0
REUSE_BUILD=0
CLEAN_BUILD=0
IO_THREADS="${PERF_IO_THREADS:-}"
HWM="${PERF_SINGLE_HWM:-}"
SEND_HWM="${PERF_SINGLE_SNDHWM:-${HWM}}"
RECV_HWM="${PERF_SINGLE_RCVHWM:-${HWM}}"
SNDBUF="${PERF_SINGLE_SNDBUF:-${PERF_SNDBUF:-}}"
RCVBUF="${PERF_SINGLE_RCVBUF:-${PERF_RCVBUF:-}}"
SNDTIMEO_MS="${PERF_SINGLE_SNDTIMEO_MS:-200}"
RCVTIMEO_MS="${PERF_SINGLE_RCVTIMEO_MS:-200}"
TRANSPORT_TRANSITION_MS="${PERF_TRANSPORT_TRANSITION_MS:-3000}"
RUN_COOLDOWN_MS="${PERF_SINGLE_RUN_COOLDOWN_MS:-3000}"
CTX_AUTO_HWM_ENABLE="${PERF_CTX_AUTO_HWM_ENABLE:-1}"
CTX_AUTO_HWM_PROFILE="${PERF_SINGLE_CTX_AUTO_HWM_PROFILE:-${PERF_CTX_AUTO_HWM_PROFILE:-balanced}}"

usage() {
  cat <<'USAGE'
Usage: perf/single/run_benchmarks.sh [options]

Options:
  -h, --help            Show this help.
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --runs N               Iterations per pattern/transport/size.
  --duration N           Active duration seconds.
  --run-cooldown-ms N    Cooldown between repeated runs.
  --build-dir PATH       Build directory override.
  --reuse-build          Reuse existing installDist output.
  --clean-build          Delete build dir before installDist.
  --output PATH          Tee report output to PATH.
  --pin-cpu              Pin benchmark process to CPU 0 on Linux.
  --io-threads N         Context I/O threads.
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
  --auto-hwm-profile NAME Auto-HWM profile.
  --results-dir PATH     Results root override.
  --results-tag NAME     Optional report suffix tag.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern) PATTERN="${2:-}"; shift ;;
    --transports) TRANSPORTS="${2:-}"; shift ;;
    --msg-sizes) MSG_SIZES="${2:-}"; shift ;;
    --runs) RUNS="${2:-}"; shift ;;
    --duration) DURATION="${2:-}"; shift ;;
    --run-cooldown-ms) RUN_COOLDOWN_MS="${2:-}"; shift ;;
    --build-dir) BUILD_DIR="${2:-}"; shift ;;
    --reuse-build) REUSE_BUILD=1 ;;
    --clean-build) CLEAN_BUILD=1 ;;
    --output) OUTPUT_PATH="${2:-}"; shift ;;
    --pin-cpu) PIN_CPU=1 ;;
    --io-threads) IO_THREADS="${2:-}"; shift ;;
    --hwm) HWM="${2:-}"; SEND_HWM="${2:-}"; RECV_HWM="${2:-}"; shift ;;
    --send-hwm) SEND_HWM="${2:-}"; shift ;;
    --recv-hwm) RECV_HWM="${2:-}"; shift ;;
    --buf) SNDBUF="${2:-}"; RCVBUF="${2:-}"; shift ;;
    --sndbuf) SNDBUF="${2:-}"; shift ;;
    --rcvbuf) RCVBUF="${2:-}"; shift ;;
    --auto-hwm-profile) CTX_AUTO_HWM_PROFILE="${2:-}"; shift ;;
    --sndtimeo|--send-timeout-ms) SNDTIMEO_MS="${2:-}"; shift ;;
    --rcvtimeo|--recv-timeout-ms) RCVTIMEO_MS="${2:-}"; shift ;;
    --results-dir) RESULTS_ROOT="${2:-}"; shift ;;
    --results-tag) RESULTS_TAG="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

failure_reason_from_output() {
  local output_text="$1"
  python3 - "$output_text" <<'PY'
import sys

for raw in sys.argv[1].splitlines():
    if not raw.startswith("FAIL,"):
        continue
    parts = raw.strip().split(",", 5)
    if len(parts) == 6 and parts[5]:
        print(parts[5])
        raise SystemExit(0)
print("process_exit_nonzero")
PY
}

if ! [[ "${RUNS}" =~ ^[0-9]+$ ]] || [[ "${RUNS}" -lt 1 ]]; then
  echo "--runs must be >= 1" >&2
  exit 1
fi

for numeric_opt in IO_THREADS SEND_HWM RECV_HWM; do
  value="${!numeric_opt}"
  if [[ -n "${value}" ]] && { ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 1 ]]; }; then
    echo "${numeric_opt,,} must be >= 1" >&2
    exit 1
  fi
done

if [[ -n "${HWM}${SEND_HWM}${RECV_HWM}${SNDBUF}${RCVBUF}" \
  && "${PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES:-${PERF_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}}" != "1" ]]; then
  echo "manual HWM/SNDBUF/RCVBUF overrides are debug-only; set PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES=1" >&2
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
if [[ "${PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES:-${PERF_ALLOW_MANUAL_SOCKET_OVERRIDES:-0}}" == "1" ]]; then
  export PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES=1
fi

for numeric_opt in SNDTIMEO_MS RCVTIMEO_MS TRANSPORT_TRANSITION_MS RUN_COOLDOWN_MS; do
  value="${!numeric_opt}"
  if [[ -n "${value}" ]] && { ! [[ "${value}" =~ ^[0-9]+$ ]] || [[ "${value}" -lt 0 ]]; }; then
    echo "${numeric_opt,,} must be >= 0" >&2
    exit 1
  fi
done

if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERN="PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT"
fi

detect_platform() {
  case "$(uname -s)" in
    Linux*) echo "linux" ;;
    Darwin*) echo "macos" ;;
    MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
    *) echo "$(uname -s | tr '[:upper:]' '[:lower:]')" ;;
  esac
}

default_transports() {
  case "$1" in
    SPOT) echo "tcp,tls,ws,wss" ;;
    *)
      if [[ "$(uname -s)" == "Linux" ]]; then
        echo "tcp,tls,ws,wss,inproc,ipc"
      else
        echo "tcp,tls,ws,wss,inproc"
      fi
      ;;
  esac
}

trim_csv() {
  printf '%s' "$1" | awk -F',' '{for (i=1; i<=NF; ++i) {gsub(/^[[:space:]]+|[[:space:]]+$/, "", $i); printf "%s%s", (i>1?",":""), $i}}'
}

prune_reports() {
  local report_dir="$1"
  local count
  count="$(find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' | wc -l | tr -d ' ')"
  if [[ -z "${count}" || "${count}" -le 100 ]]; then
    return
  fi
  find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' -printf '%f\n' \
    | sort \
    | head -n "$((count - 100))" \
    | while read -r old_file; do
        rm -f "${report_dir}/${old_file}"
      done
}

sleep_ms() {
  python3 - "$1" <<'PY'
import sys
import time
time.sleep(int(sys.argv[1]) / 1000.0)
PY
}

resolve_build_dir() {
  if [[ -n "${BUILD_DIR}" ]]; then
    printf '%s' "${BUILD_DIR%/}/perf-single"
  else
    printf '%s' "${ROOT_DIR}/single/Zlink.BindingBench/build"
  fi
}

ensure_single_runner() {
  local build_dir="$1"
  local runner_path="$2"
  local install_dir="${build_dir}/install"
  local dist_zip="${build_dir}/distributions/zlink-java-perf-single.zip"
  if [[ -x "${runner_path}" && -s "${runner_path}" ]]; then
    return 0
  fi
  rm -f "${runner_path}"
  if [[ -f "${dist_zip}" ]]; then
    mkdir -p "${install_dir}"
    unzip -qo "${dist_zip}" -d "${install_dir}"
  fi
}

mkdir -p "${RESULTS_ROOT}/single/report"
cd "${ROOT_DIR}"
PROJECT_BUILD_DIR="$(resolve_build_dir)"
RUNNER="${PROJECT_BUILD_DIR}/install/zlink-java-perf-single/bin/zlink-java-perf-single"
if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
  rm -rf "${PROJECT_BUILD_DIR}"
fi
ensure_single_runner "${PROJECT_BUILD_DIR}" "${RUNNER}"
if [[ "${REUSE_BUILD}" -eq 0 ]]; then
  "${JAVA_BINDINGS_DIR}/gradlew" --no-daemon -p "${JAVA_BINDINGS_DIR}" \
    -PzlinkPerfBuildDir="${PROJECT_BUILD_DIR}" :perf-single:installDist >/dev/null
fi
ensure_single_runner "${PROJECT_BUILD_DIR}" "${RUNNER}"
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
report="${RESULTS_ROOT}/single/report/perf_java_single_${platform}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

tmp_metrics="$(mktemp)"
tmp_failures="$(mktemp)"
tmp_auto_hwm="$(mktemp)"
trap 'rm -f "${tmp_metrics}" "${tmp_failures}" "${tmp_auto_hwm}"' EXIT

metrics_regex='^(throughput|bandwidth|latency|latency_p95|latency_p99)$'
runner_cmd=("${RUNNER}")
if [[ "${PIN_CPU}" -eq 1 ]]; then
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "--pin-cpu is only supported on Linux in this runner" >&2
    exit 1
  fi
  if ! command -v taskset >/dev/null 2>&1; then
    echo "--pin-cpu requires taskset" >&2
    exit 1
  fi
  runner_cmd=("taskset" "-c" "0" "${RUNNER}")
fi

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

format_progress_row_from_output() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local prefix="$4"
  local output_text="$5"
  python3 - "$pattern" "$transport" "$size" "$prefix" "$output_text" <<'PY'
import sys

pattern, transport, size, prefix, output_text = sys.argv[1:]
size = int(size)
unit = "Kops/s" if pattern in {"DEALER_ROUTER", "ROUTER_ROUTER"} else "Kmsg/s"
metrics = {}
for line in output_text.splitlines():
    if not line.startswith("RESULT,"):
        continue
    parts = line.strip().split(",")
    if len(parts) != 7:
        continue
    _, _, result_pattern, result_transport, result_size, metric, value = parts
    if result_pattern == pattern and result_transport == transport and int(result_size) == size:
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

IFS=',' read -r -a patterns <<< "$(trim_csv "${PATTERN}")"
IFS=',' read -r -a msg_sizes <<< "$(trim_csv "${MSG_SIZES}")"

for pattern in "${patterns[@]}"; do
  current_transports="${TRANSPORTS:-$(default_transports "${pattern}")}"
  IFS=',' read -r -a transports <<< "$(trim_csv "${current_transports}")"
  for transport_index in "${!transports[@]}"; do
    transport="${transports[$transport_index]}"
    echo "    Testing ${transport} | ${MSG_SIZES}:"
    print_table_header "      "
    for size in "${msg_sizes[@]}"; do
      for ((run=1; run<=RUNS; run++)); do
        expected_result_lines=$((expected_result_lines + 5))
        cmd=("${runner_cmd[@]}" "${pattern}" "${transport}" "${size}" \
          --duration "${DURATION}")
        if [[ -n "${IO_THREADS}" ]]; then
          cmd+=(--io-threads "${IO_THREADS}")
        fi
        if [[ -n "${SEND_HWM}" ]]; then
          cmd+=(--send-hwm "${SEND_HWM}")
        fi
        if [[ -n "${RECV_HWM}" ]]; then
          cmd+=(--recv-hwm "${RECV_HWM}")
        fi
        if [[ -n "${SNDBUF}" ]]; then
          cmd+=(--sndbuf "${SNDBUF}")
        fi
        if [[ -n "${RCVBUF}" ]]; then
          cmd+=(--rcvbuf "${RCVBUF}")
        fi
        cmd+=(--sndtimeo "${SNDTIMEO_MS}" --rcvtimeo "${RCVTIMEO_MS}")
        if ! output="$("${cmd[@]}" 2>&1)"; then
          if printf '%s\n' "${output}" | grep -q '^UNSUPPORTED,'; then
            expected_result_lines=$((expected_result_lines - 5))
            continue
          fi
          record_failure "${pattern}" "${transport}" "${size}" "${run}" \
            "$(failure_reason_from_output "${output}")"
          continue
        fi
        if (( RUNS > 1 )); then
          printf '      run %s/%s:\n' "${run}" "${RUNS}"
        fi
        if printf '%s\n' "${output}" | grep -q '^UNSUPPORTED,'; then
          expected_result_lines=$((expected_result_lines - 5))
          continue
        fi
        while IFS= read -r line; do
          if [[ "${line}" == AUTO_HWM_DETAIL,* ]]; then
            printf '%s\n' "${line}" >> "${tmp_auto_hwm}"
            continue
          fi
          [[ "${line}" == RESULT,* ]] || continue
          IFS=',' read -r tag lib result_pattern result_transport result_size metric value <<< "${line}"
          if [[ ! "${metric}" =~ ${metrics_regex} ]]; then
            continue
          fi
          printf '%s,%s,%s,%s,%s,%s\n' \
            "${pattern}" "${transport}" "${size}" "${run}" "${metric}" "${value}" >> "${tmp_metrics}"
          actual_result_lines=$((actual_result_lines + 1))
        done <<< "${output}"
        required_count="$(printf '%s\n' "${output}" \
          | awk -F',' '/^RESULT,/ && ($6=="throughput" || $6=="bandwidth" || $6=="latency" || $6=="latency_p95" || $6=="latency_p99") {count++} END {print count+0}')"
        if [[ "${required_count}" -ne 5 ]]; then
          record_failure "${pattern}" "${transport}" "${size}" "${run}" "missing_required_result_lines"
        else
          row="$(format_progress_row_from_output "${pattern}" "${transport}" "${size}" "      " "${output}")"
          echo "${row}"
        fi
        if (( run < RUNS )); then
          echo "[cooldown ${RUN_COOLDOWN_MS}ms]"
          sleep_ms "${RUN_COOLDOWN_MS}"
        fi
      done
    done
    if [[ "${TRANSPORT_TRANSITION_MS}" -gt 0 && "$((transport_index + 1))" -lt "${#transports[@]}" ]]; then
      sleep_ms "${TRANSPORT_TRANSITION_MS}"
    fi
  done
done

python_status=0
python3 - "${ROOT_DIR}/report_common.py" "${tmp_metrics}" "${tmp_failures}" "${tmp_auto_hwm}" "${report}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${PIN_CPU}" "${expected_result_lines}" "${actual_result_lines}" \
  "${IO_THREADS}" "${HWM}" "${SEND_HWM}" "${RECV_HWM}" "${SNDTIMEO_MS}" \
  "${RCVTIMEO_MS}" "${SNDBUF}" "${RCVBUF}" \
  "${CTX_AUTO_HWM_ENABLE}" "${CTX_AUTO_HWM_PROFILE}" "${OUTPUT_PATH}" <<'PY' || python_status=$?
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

helper_path, metrics_path, failures_path, auto_hwm_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, runs, duration, results_tag, pin_cpu, expected_result_lines, actual_result_lines, io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, sndbuf, rcvbuf, ctx_auto_hwm_enable, ctx_auto_hwm_profile, output_path = sys.argv[1:]
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
    return f"{value / 1000.0:.3f} Kmsg/s"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} MB/s"

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} ms"

def fmt_size(size):
    return f"{size}B"

def bytes_to_kb(value):
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        return "?"
    if parsed <= 0:
        return "0"
    if parsed % 1024 == 0:
        return str(parsed // 1024)
    return f"{parsed / 1024.0:.1f}"

def emit_table(columns, rows):
    widths = []
    for header, key in columns:
        width = len(header)
        for row in rows:
            width = max(width, len(str(row.get(key, "?"))))
        widths.append(width)
    emit("| " + " | ".join(
        f"{columns[i][0]:<{widths[i]}}" for i in range(len(columns))
    ) + " |")
    emit("|-" + "-|-".join("-" * width for width in widths) + "-|")
    for row in rows:
        emit("| " + " | ".join(
            f"{str(row.get(columns[i][1], '?')):<{widths[i]}}"
            for i in range(len(columns))
        ) + " |")

def emit_single_auto_hwm(pattern):
    selected = []
    seen = set()
    for row in auto_hwm_rows:
        if row.get("pattern", "").upper() != pattern.upper():
            continue
        display = dict(row)
        display["sndbuf_kb"] = bytes_to_kb(row.get("effective_sndbuf", ""))
        display["rcvbuf_kb"] = bytes_to_kb(row.get("effective_rcvbuf", ""))
        key = tuple(display.get(name, "") for name in (
            "msg_size", "component", "owner", "socket", "socket_type",
            "role", "sndhwm", "rcvhwm", "sndbuf_kb", "rcvbuf_kb",
            "effective_message_bytes", "socket_message_slots",
        ))
        if key in seen:
            continue
        seen.add(key)
        selected.append(display)
    if not selected:
        return
    selected.sort(key=lambda row: (
        int(row.get("msg_size", "0") or "0"),
        row.get("component", ""),
        row.get("owner", ""),
        row.get("socket", ""),
    ))
    emit("## Auto-HWM Detail")
    emit(f"- pattern: {pattern}")
    emit_table((
        ("Size(B)", "msg_size"),
        ("Component", "component"),
        ("Owner", "owner"),
        ("Socket", "socket"),
        ("Type", "socket_type"),
        ("Role", "role"),
        ("SNDHWM", "sndhwm"),
        ("RCVHWM", "rcvhwm"),
        ("SNDBUF(KB)", "sndbuf_kb"),
        ("RCVBUF(KB)", "rcvbuf_kb"),
        ("MsgUnit(B)", "effective_message_bytes"),
        ("Slots", "socket_message_slots"),
    ), selected)
    emit("")

lines = []

def emit(line=""):
    lines.append(line)

start_options = [
    ("runs", runs),
    ("duration_seconds", duration),
    ("patterns", pattern_csv),
    ("transports", transports_csv or "default-per-pattern"),
    ("msg_sizes", msg_sizes_csv),
    ("pin_cpu", "on" if pin_cpu == "1" else "off"),
    ("io_threads", io_threads or "default(binding)"),
    ("hwm", hwm or "auto-hwm"),
    ("sndhwm", send_hwm or "auto-hwm"),
    ("rcvhwm", recv_hwm or "auto-hwm"),
    ("sndbuf", sndbuf or "auto-hwm"),
    ("rcvbuf", rcvbuf or "auto-hwm"),
    ("sndtimeo_ms", sndtimeo_ms),
    ("rcvtimeo_ms", rcvtimeo_ms),
    ("ctx_auto_hwm_enable", ctx_auto_hwm_enable),
    ("ctx_auto_hwm_profile", ctx_auto_hwm_profile),
]
if results_tag:
    start_options.append(("results_tag", results_tag))

emit_effective_options(lines, "start", "java", "single", start_options)
emit("===============================================================================")
emit("")

result_lines = []
for pattern in patterns:
    emit(f"## PATTERN: {pattern}")
    emit("")
    for transport in pattern_transports[pattern]:
        emit(f"### Transport: {transport}")
        emit("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |")
        emit("|----------|--------------------|----------------|---------------|---------------|---------------|")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in all_metrics}
            emit(
                f"| {fmt_size(size):<8} | {fmt_rate(metric_values['throughput']):>16} | "
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

for pattern in patterns:
    emit_single_auto_hwm(pattern)

emit_effective_options(lines, "result", "java", "single", start_options)
emit("")
emit("## Result Data")
for line in result_lines:
    emit(line)
emit("")
status = "complete" if expected_result_lines == actual_result_lines and not failures else "partial"
emit_completion(lines, status, expected_result_lines, actual_result_lines)
emit_failures(lines, failures)
text = write_report(lines, report_path, output_path)
sys.stdout.write(text)
sys.exit(0 if status == "complete" else 1)
PY

prune_reports "${RESULTS_ROOT}/single/report"
if [[ -n "${OUTPUT_PATH}" ]]; then
  echo "saved report: ${report}" | tee -a "${OUTPUT_PATH}"
else
  echo "saved report: ${report}"
fi
exit "${python_status}"
