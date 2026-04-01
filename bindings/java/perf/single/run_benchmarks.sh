#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUNNER="${ROOT_DIR}/perf/single/Zlink.BindingBench/build/install/zlink-java-perf-single/bin/zlink-java-perf-single"
RESULTS_ROOT="${ROOT_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS=""
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
RECV_MODE="callback"
RUNS=1
WARMUP="${PERF_SINGLE_WARMUP_SECONDS:-2}"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
RESULTS_TAG=""
REQUIRED_METRICS="throughput bandwidth latency latency_p95 latency_p99"
INFO_METRICS="cpu_pct mem_mb snd_pending_max rcv_pending_max rcv_pending_end"

usage() {
  cat <<'USAGE'
Usage: perf/single/run_benchmarks.sh [options]

Options:
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --recv MODE            callback only.
  --runs N               Iterations per pattern/transport/size.
  --warmup N             Warmup seconds.
  --duration N           Active duration seconds.
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
    --warmup) WARMUP="${2:-}"; shift ;;
    --duration) DURATION="${2:-}"; shift ;;
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

mkdir -p "${RESULTS_ROOT}/single/report"
"${ROOT_DIR}/gradlew" :perf-single:installDist >/dev/null

platform="$(detect_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report="${RESULTS_ROOT}/single/report/perf_${platform}_${RECV_MODE}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

tmp_metrics="$(mktemp)"
trap 'rm -f "${tmp_metrics}"' EXIT

metrics_regex='^(throughput|bandwidth|latency|latency_p95|latency_p99|cpu_pct|mem_mb|snd_pending_max|rcv_pending_max|rcv_pending_end)$'

IFS=',' read -r -a patterns <<< "$(trim_csv "${PATTERN}")"
IFS=',' read -r -a msg_sizes <<< "$(trim_csv "${MSG_SIZES}")"

for pattern in "${patterns[@]}"; do
  current_transports="${TRANSPORTS:-$(default_transports "${pattern}")}"
  IFS=',' read -r -a transports <<< "$(trim_csv "${current_transports}")"
  for transport in "${transports[@]}"; do
    for size in "${msg_sizes[@]}"; do
      for ((run=1; run<=RUNS; run++)); do
        output="$("${RUNNER}" "${pattern}" "${transport}" "${size}" \
          --warmup "${WARMUP}" --duration "${DURATION}" --recv "${RECV_MODE}")"
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
  "${RECV_MODE}" "${RUNS}" "${WARMUP}" "${DURATION}" "${RESULTS_TAG}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, recv_mode, runs, warmup, duration, results_tag = sys.argv[1:]
runs = int(runs)
required_metrics = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
info_metrics = ["cpu_pct", "mem_mb", "snd_pending_max", "rcv_pending_max", "rcv_pending_end"]
all_metrics = required_metrics + info_metrics

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

def avg(values):
    usable = [v for v in values if not math.isnan(v)]
    if not usable:
        return math.nan
    return sum(usable) / len(usable)

def fmt_metric(value):
    return "N/A" if math.isnan(value) else f"{value:.2f}"

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
    emit("- pin_cpu: off")
    emit(f"- warmup_seconds: {warmup}")
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
        emit("| Size | Throughput | Bandwidth | Lat.Mean | Lat.P95 | Lat.P99 | CPU% | Mem MB |")
        emit("|------|------------|-----------|----------|---------|---------|------|--------|")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            metric_values = {metric: avg(rows[key].get(metric, [])) for metric in all_metrics}
            expected += 5
            if all(rows[key].get(metric) for metric in required_metrics):
                actual += 5
            emit(
                f"| {fmt_size(size)} | {fmt_rate(metric_values['throughput'])} | "
                f"{fmt_bandwidth(metric_values['bandwidth'])} | "
                f"{fmt_latency_us(metric_values['latency'])} | "
                f"{fmt_latency_us(metric_values['latency_p95'])} | "
                f"{fmt_latency_us(metric_values['latency_p99'])} | "
                f"{fmt_metric(metric_values['cpu_pct'])} | {fmt_metric(metric_values['mem_mb'])} |"
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

text = "\n".join(lines) + "\n"
with open(report_path, "w", encoding="utf-8") as report_file:
    report_file.write(text)
sys.stdout.write(text)
PY

prune_reports "${RESULTS_ROOT}/single/report"
echo "saved report: ${report}"
