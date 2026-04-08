#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RESULTS_ROOT="${ROOT_DIR}/results"
PATTERN="ALL"
TRANSPORTS=""
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
RECV_MODE="callback"
RUNS=1
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
RESULTS_TAG=""
BUILD_DIR=""
OUTPUT_PATH=""
PIN_CPU=0
REUSE_BUILD=0
CLEAN_BUILD=0
IO_THREADS="${PERF_IO_THREADS:-}"
HWM="${PERF_SINGLE_HWM:-}"
SEND_HWM="${PERF_SINGLE_SNDHWM:-${HWM}}"
RECV_HWM="${PERF_SINGLE_RCVHWM:-${HWM}}"
SNDTIMEO_MS="${PERF_SINGLE_SNDTIMEO_MS:-200}"
RCVTIMEO_MS="${PERF_SINGLE_RCVTIMEO_MS:-200}"

usage() {
  cat <<'USAGE'
Usage: perf/single/run_benchmarks.sh [options]

Options:
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --recv MODE            callback only.
  --runs N               Iterations per pattern/transport/size.
  --duration N           Active duration seconds.
  --build-dir PATH       Build directory override.
  --reuse-build          Reuse existing installDist output.
  --clean-build          Delete build dir before installDist.
  --output PATH          Tee report output to PATH.
  --pin-cpu              Pin benchmark process to CPU 0 on Linux.
  --io-threads N         Context I/O threads.
  --hwm N                Shared HWM fallback.
  --send-hwm N           Send HWM override.
  --recv-hwm N           Receive HWM override.
  --sndtimeo N           Send timeout ms.
  --rcvtimeo N           Receive timeout ms.
  --results-dir PATH     Results root override.
  --results-tag NAME     Optional report suffix tag.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern) PATTERN="${2:-}"; shift ;;
    --transports) TRANSPORTS="${2:-}"; shift ;;
    --msg-sizes) MSG_SIZES="${2:-}"; shift ;;
    --recv) RECV_MODE="${2:-}"; shift ;;
    --runs) RUNS="${2:-}"; shift ;;
    --duration) DURATION="${2:-}"; shift ;;
    --build-dir) BUILD_DIR="${2:-}"; shift ;;
    --reuse-build) REUSE_BUILD=1 ;;
    --clean-build) CLEAN_BUILD=1 ;;
    --output) OUTPUT_PATH="${2:-}"; shift ;;
    --pin-cpu) PIN_CPU=1 ;;
    --io-threads) IO_THREADS="${2:-}"; shift ;;
    --hwm) HWM="${2:-}"; SEND_HWM="${2:-}"; RECV_HWM="${2:-}"; shift ;;
    --send-hwm) SEND_HWM="${2:-}"; shift ;;
    --recv-hwm) RECV_HWM="${2:-}"; shift ;;
    --sndtimeo|--send-timeout-ms) SNDTIMEO_MS="${2:-}"; shift ;;
    --rcvtimeo|--recv-timeout-ms) RCVTIMEO_MS="${2:-}"; shift ;;
    --results-dir) RESULTS_ROOT="${2:-}"; shift ;;
    --results-tag) RESULTS_TAG="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if [[ "${RECV_MODE}" != "callback" ]]; then
  echo "single suite only supports --recv callback" >&2
  exit 1
fi

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

for numeric_opt in SNDTIMEO_MS RCVTIMEO_MS; do
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

resolve_build_dir() {
  if [[ -n "${BUILD_DIR}" ]]; then
    printf '%s' "${BUILD_DIR%/}/perf-single"
  else
    printf '%s' "${ROOT_DIR}/single/Zlink.BindingBench/build"
  fi
}

mkdir -p "${RESULTS_ROOT}/single/report"
cd "${ROOT_DIR}"
PROJECT_BUILD_DIR="$(resolve_build_dir)"
if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
  rm -rf "${PROJECT_BUILD_DIR}"
fi
if [[ "${REUSE_BUILD}" -eq 0 ]]; then
  gradle -PzlinkPerfBuildDir="${PROJECT_BUILD_DIR}" :perf-single:installDist >/dev/null
fi
RUNNER="${PROJECT_BUILD_DIR}/install/zlink-java-perf-single/bin/zlink-java-perf-single"
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
report="${RESULTS_ROOT}/single/report/perf_${platform}_${RECV_MODE}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

tmp_metrics="$(mktemp)"
trap 'rm -f "${tmp_metrics}"' EXIT

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

IFS=',' read -r -a patterns <<< "$(trim_csv "${PATTERN}")"
IFS=',' read -r -a msg_sizes <<< "$(trim_csv "${MSG_SIZES}")"

for pattern in "${patterns[@]}"; do
  current_transports="${TRANSPORTS:-$(default_transports "${pattern}")}"
  IFS=',' read -r -a transports <<< "$(trim_csv "${current_transports}")"
  for transport in "${transports[@]}"; do
    for size in "${msg_sizes[@]}"; do
      for ((run=1; run<=RUNS; run++)); do
        cmd=("${runner_cmd[@]}" "${pattern}" "${transport}" "${size}" \
          --duration "${DURATION}" --recv "${RECV_MODE}")
        if [[ -n "${IO_THREADS}" ]]; then
          cmd+=(--io-threads "${IO_THREADS}")
        fi
        if [[ -n "${SEND_HWM}" ]]; then
          cmd+=(--send-hwm "${SEND_HWM}")
        fi
        if [[ -n "${RECV_HWM}" ]]; then
          cmd+=(--recv-hwm "${RECV_HWM}")
        fi
        cmd+=(--sndtimeo "${SNDTIMEO_MS}" --rcvtimeo "${RCVTIMEO_MS}")
        output="$("${cmd[@]}")"
        while IFS= read -r line; do
          [[ "${line}" == RESULT,* ]] || continue
          IFS=',' read -r tag lib result_pattern result_transport result_size metric value <<< "${line}"
          if [[ ! "${metric}" =~ ${metrics_regex} ]]; then
            continue
          fi
          printf '%s,%s,%s,%s,%s,%s\n' \
            "${pattern}" "${transport}" "${size}" "${run}" "${metric}" "${value}" >> "${tmp_metrics}"
        done <<< "${output}"
        required_count="$(printf '%s\n' "${output}" \
          | awk -F',' '/^RESULT,/ && ($6=="throughput" || $6=="bandwidth" || $6=="latency" || $6=="latency_p95" || $6=="latency_p99") {count++} END {print count+0}')"
        if [[ "${required_count}" -ne 5 ]]; then
          echo "missing required RESULT lines for ${pattern}/${transport}/${size} run=${run}" >&2
          exit 1
        fi
      done
    done
  done
done

python3 - "${tmp_metrics}" "${report}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${RECV_MODE}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${PIN_CPU}" \
  "${IO_THREADS}" "${HWM}" "${SEND_HWM}" "${RECV_HWM}" "${SNDTIMEO_MS}" \
  "${RCVTIMEO_MS}" "${OUTPUT_PATH}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, recv_mode, runs, duration, results_tag, pin_cpu, io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, output_path = sys.argv[1:]
runs = int(runs)
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
    return f"{value / 1000.0:.2f} Kmsg/s"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} MB/s"

def fmt_latency_us(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} us"

def fmt_size(size):
    return f"{size}B"

expected = 0
actual = 0
lines = []

def emit(line=""):
    lines.append(line)

def emit_effective_options(section):
    emit(f"## Effective Options ({section})")
    emit("- suite: single")
    emit(f"- runs: {runs}")
    emit(f"- patterns: {pattern_csv}")
    emit(f"- transports: {transports_csv or 'default-per-pattern'}")
    emit(f"- msg_sizes: {msg_sizes_csv}")
    emit(f"- recv_mode: {recv_mode}")
    emit(f"- pin_cpu: {'on' if pin_cpu == '1' else 'off'}")
    emit(f"- io_threads: {io_threads or 'default(binding)'}")
    emit(f"- hwm: {hwm or 'default(binding)'}")
    emit(f"- send_hwm: {send_hwm or 'default(binding)'}")
    emit(f"- recv_hwm: {recv_hwm or 'default(binding)'}")
    emit(f"- send_timeout_ms: {sndtimeo_ms}")
    emit(f"- recv_timeout_ms: {rcvtimeo_ms}")
    emit(f"- duration_seconds: {duration}")
    if results_tag:
        emit(f"- results_tag: {results_tag}")
    emit("")

emit_effective_options("start")
emit("===============================================================================")
emit("")

result_lines = []
for pattern in patterns:
    emit(f"## PATTERN: {pattern}")
    emit("")
    for transport in pattern_transports[pattern]:
        emit(f"### Transport: {transport}")
        emit("| Size | Throughput | Bandwidth | Lat.Mean | Lat.P95 | Lat.P99 |")
        emit("|------|------------|-----------|----------|---------|---------|")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in all_metrics}
            expected += 5
            if all(rows[key].get(metric) for metric in required_metrics):
                actual += 5
            emit(
                f"| {fmt_size(size)} | {fmt_rate(metric_values['throughput'])} | "
                f"{fmt_bandwidth(metric_values['bandwidth'])} | "
                f"{fmt_latency_us(metric_values['latency'])} | "
                f"{fmt_latency_us(metric_values['latency_p95'])} | "
                f"{fmt_latency_us(metric_values['latency_p99'])} |"
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
if output_path:
    with open(output_path, "a", encoding="utf-8") as output_file:
        output_file.write(text)
sys.stdout.write(text)
PY

prune_reports "${RESULTS_ROOT}/single/report"
if [[ -n "${OUTPUT_PATH}" ]]; then
  echo "saved report: ${report}" | tee -a "${OUTPUT_PATH}"
else
  echo "saved report: ${report}"
fi
