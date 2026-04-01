#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
REPO_DIR="$(cd "${ROOT_DIR}/../.." && pwd)"
RUNNER="${ROOT_DIR}/perf/multi/Zlink.BindingBench.Multi/build/install/zlink-java-perf-multi/bin/zlink-java-perf-multi"
STREAM_CLIENT="${REPO_DIR}/core/build/bin/perf_stream_client"
RESULTS_ROOT="${ROOT_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS="${PERF_TRANSPORTS:-tcp,tls,ws,wss}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
RECV_MODE="recv"
CLIENTS="${PERF_MULTI_CLIENTS:-100}"
RUNS=1
WARMUP="${PERF_MULTI_WARMUP_SECONDS:-2}"
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
RESULTS_TAG=""
STREAM_DEFAULT_CLIENTS=10000
STREAM_DEFAULT_MSG_SIZES="64,256,1024,65536"
explicit_clients=0
explicit_msg_sizes=0

usage() {
  cat <<'USAGE'
Usage: perf/multi/run_benchmarks.sh [options]

Options:
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --recv MODE            recv|callback.
  --clients N            Client count.
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
    --msg-sizes) MSG_SIZES="${2:-}"; explicit_msg_sizes=1; shift ;;
    --recv) RECV_MODE="${2:-}"; shift ;;
    --clients) CLIENTS="${2:-}"; explicit_clients=1; shift ;;
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

if [[ "${RECV_MODE}" != "recv" && "${RECV_MODE}" != "callback" ]]; then
  echo "multi suite supports only --recv recv|callback" >&2
  exit 1
fi

if ! [[ "${RUNS}" =~ ^[0-9]+$ ]] || [[ "${RUNS}" -lt 1 ]]; then
  echo "--runs must be >= 1" >&2
  exit 1
fi

if ! [[ "${CLIENTS}" =~ ^[0-9]+$ ]] || [[ "${CLIENTS}" -lt 1 ]]; then
  echo "--clients must be >= 1" >&2
  exit 1
fi

if [[ "${PATTERN}" == "ALL" ]]; then
  if [[ "${RECV_MODE}" == "callback" ]]; then
    PATTERN="MULTI_SPOT,MULTI_STREAM"
  else
    PATTERN="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_STREAM"
  fi
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
  python3 - "$endpoint" <<'PY'
import socket, sys, time
endpoint = sys.argv[1]
host_port = endpoint.split("://", 1)[1]
host, port = host_port.rsplit(":", 1)
port = int(port)
deadline = time.time() + 10.0
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

validate_pattern_mode() {
  local bare_pattern="$1"
  if [[ "${RECV_MODE}" == "callback" && "${bare_pattern}" != "SPOT" && "${bare_pattern}" != "STREAM" ]]; then
    echo "${bare_pattern} does not support --recv callback in multi suite" >&2
    exit 1
  fi
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

mkdir -p "${RESULTS_ROOT}/multi/tmp" "${RESULTS_ROOT}/multi/report"
"${ROOT_DIR}/gradlew" :perf-multi:installDist >/dev/null

platform="$(detect_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report="${RESULTS_ROOT}/multi/report/perf_${platform}_${RECV_MODE}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

tmp_metrics="$(mktemp)"
trap 'rm -f "${tmp_metrics}"' EXIT
metrics_regex='^(throughput|bandwidth|latency|latency_p95|latency_p99|cpu_pct|mem_mb|snd_pending_max|rcv_pending_max|rcv_pending_end)$'

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
IFS=',' read -r -a transports <<< "$(trim_csv "${TRANSPORTS}")"
display_msg_sizes="${MSG_SIZES}"
display_clients="${CLIENTS}"

if printf '%s\n' "${patterns[@]}" | grep -qx 'MULTI_STREAM'; then
  if [[ "${explicit_msg_sizes}" -eq 0 ]]; then
    display_msg_sizes="${MSG_SIZES} (STREAM: ${STREAM_DEFAULT_MSG_SIZES})"
  fi
  if [[ "${explicit_clients}" -eq 0 && "${CLIENTS}" == "100" ]]; then
    display_clients="${CLIENTS} (STREAM: ${STREAM_DEFAULT_CLIENTS})"
  fi
fi

for pattern in "${patterns[@]}"; do
  bare_pattern="${pattern#MULTI_}"
  validate_pattern_mode "${bare_pattern}"
  pattern_clients="$(default_clients_for_pattern "${pattern}")"
  pattern_msg_sizes="$(default_msg_sizes_for_pattern "${pattern}")"
  IFS=',' read -r -a msg_sizes <<< "$(trim_csv "${pattern_msg_sizes}")"

  for transport in "${transports[@]}"; do
    for size in "${msg_sizes[@]}"; do
      for ((run=1; run<=RUNS; run++)); do
        server_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_server.log"
        client_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_client.log"
        rm -f "${server_log}" "${client_log}"

        if [[ "${bare_pattern}" == "STREAM" ]]; then
          fifo="${RESULTS_ROOT}/multi/tmp/stream_control_${transport}_${size}.fifo"
          rm -f "${fifo}"
          mkfifo "${fifo}"
          endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
          exec 3<>"${fifo}"
          "${RUNNER}" --multi-server "${pattern}" "${transport}" "${size}" \
            --recv "${RECV_MODE}" --endpoint "${endpoint}" --clients "${pattern_clients}" \
            --warmup "${WARMUP}" --duration "${DURATION}" --control-port 0 \
            <"${fifo}" >"${server_log}" 2>&1 &
          server_pid=$!
          wait_for_tcp_endpoint "${endpoint}"
          "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM \
            --sizes "${size}" --runs 1 --warmup "${WARMUP}" \
            --duration "${DURATION}" --ccu "${pattern_clients}" \
            --print-perf-result 2 --send-stop-token 1 --endpoint "${endpoint}" \
            >"${client_log}" 2>&1
          printf 'STOP\n' >&3
          exec 3>&-
          wait "${server_pid}"
          rm -f "${fifo}"
          append_metrics "${pattern}" "${transport}" "${size}" "${run}" "${client_log}"
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
        "${RUNNER}" --multi-server "${pattern}" "${transport}" "${size}" \
          --recv "${RECV_MODE}" --endpoint "${endpoint}" --clients "${pattern_clients}" \
          --warmup "${WARMUP}" --duration "${DURATION}" --control-port "${control_port}" \
          >"${server_log}" 2>&1 &
        server_pid=$!
        "${RUNNER}" --multi-client "${pattern}" "${transport}" "${size}" \
          --recv "${RECV_MODE}" --endpoint "${endpoint}" --clients "${pattern_clients}" \
          --warmup "${WARMUP}" --duration "${DURATION}" --control-port "${control_port}" \
          >"${client_log}" 2>&1
        wait "${server_pid}"
        metric_log="${server_log}"
        if [[ "${bare_pattern}" == "PUBSUB" || "${bare_pattern}" == "SPOT" ]]; then
          metric_log="${client_log}"
        fi
        append_metrics "${pattern}" "${transport}" "${size}" "${run}" "${metric_log}"
      done
    done
  done
done

python3 - "${tmp_metrics}" "${report}" "${PATTERN}" "${TRANSPORTS}" "${display_msg_sizes}" \
  "${RECV_MODE}" "${display_clients}" "${RUNS}" "${WARMUP}" "${DURATION}" "${RESULTS_TAG}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, recv_mode, clients, runs, warmup, duration, results_tag = sys.argv[1:]
runs = int(runs)
required_metrics = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
info_metrics = ["cpu_pct", "mem_mb", "snd_pending_max", "rcv_pending_max", "rcv_pending_end"]
all_metrics = required_metrics + info_metrics

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

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} ms"

def fmt_size(size):
    return f"{size}B"

expected = 0
actual = 0
lines = []

def emit(line=""):
    lines.append(line)

def emit_effective_options(section):
    emit(f"## Effective Options ({section})")
    emit("- suite: multi")
    emit(f"- runs: {runs}")
    emit(f"- patterns: {pattern_csv}")
    emit(f"- transports: {transports_csv}")
    emit(f"- msg_sizes: {msg_sizes_csv}")
    emit(f"- recv_mode: {recv_mode}")
    emit(f"- clients: {clients}")
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
        emit("| Size | Throughput | Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) | S.CPU% | S.Mem MB |")
        emit("|------|------------|-----------|--------------|-------------|-------------|--------|----------|")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            metric_values = {metric: avg(rows[key].get(metric, [])) for metric in all_metrics}
            expected += 5
            if all(rows[key].get(metric) for metric in required_metrics):
                actual += 5
            emit(
                f"| {fmt_size(size)} | {fmt_rate(metric_values['throughput'])} | "
                f"{fmt_bandwidth(metric_values['bandwidth'])} | "
                f"{fmt_latency_ms(metric_values['latency'])} | "
                f"{fmt_latency_ms(metric_values['latency_p95'])} | "
                f"{fmt_latency_ms(metric_values['latency_p99'])} | "
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

prune_reports "${RESULTS_ROOT}/multi/report"
echo "saved report: ${report}"
