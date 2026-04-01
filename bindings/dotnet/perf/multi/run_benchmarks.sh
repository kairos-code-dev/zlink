#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${DOTNET_DIR}/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj"
STREAM_CLIENT="${DOTNET_DIR}/../core/build/bin/perf_stream_client"
RESULTS_ROOT="${DOTNET_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS="${PERF_TRANSPORTS:-tcp,tls,ws,wss}"
MSG_SIZES="${PERF_MSG_SIZES:-}"
RECV_MODE="${PERF_RECV_MODE:-recv}"
CLIENTS="${PERF_MULTI_CLIENTS:-${PERF_CLIENTS:-}}"
WARMUP="${PERF_MULTI_WARMUP_SECONDS:-${PERF_WARMUP_SECONDS:-2}}"
DURATION="${PERF_MULTI_DURATION_SECONDS:-${PERF_DURATION_SECONDS:-5}}"
RUNS="${PERF_RUNS:-1}"
READY_TIMEOUT_MS="${PERF_MULTI_CONNECT_READY_TIMEOUT_MS:-${PERF_CONNECT_READY_TIMEOUT_MS:-5000}}"
RESULTS_TAG=""
CONFIGURATION="${PERF_CONFIGURATION:-Release}"
REPORT=""

usage() {
  cat <<'USAGE'
Usage: perf/multi/run_benchmarks.sh [options]

Run .NET multi-socket benchmark patterns.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --recv MODE           Receive model: recv|callback (default: recv).
  --duration N          Active duration seconds (default: 5).
  --warmup N            Warmup duration seconds (default: 2).
  --msg-sizes LIST      Message size list.
  --transports LIST     Transport list override (default: tcp,tls,ws,wss).
  --clients N           Client socket count (default: 100, stream=10000).
  --runs N              Iterations per configuration (default: 1).
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional report suffix tag.

Notes:
  - result is saved under results/multi/report/ as
    perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt.
  - callback default pattern set is MULTI_SPOT,MULTI_STREAM.
USAGE
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
  printf '%s\n' "${line}" | tee -a "${REPORT}"
}

validate_uint() {
  local label="${1:-value}"
  local value="${2:-}"
  if [[ ! "${value}" =~ ^[0-9]+$ || "${value}" -lt 1 ]]; then
    echo "${label} must be a positive integer." >&2
    exit 1
  fi
}

normalize_multi_pattern_csv() {
  local raw="${1:-}"
  local recv_mode="${2:-recv}"

  python3 - "${raw}" "${recv_mode}" <<'PY'
import sys

raw = sys.argv[1].upper()
recv_mode = sys.argv[2].lower()
allowed = {
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "SPOT",
    "STREAM",
}

if raw == "ALL":
    if recv_mode == "callback":
        print("MULTI_SPOT,MULTI_STREAM")
    else:
        print(
            "MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,"
            "MULTI_PUBSUB,MULTI_SPOT,MULTI_STREAM"
        )
    raise SystemExit(0)

items = []
for token in raw.split(","):
    value = token.strip()
    if not value:
        continue
    if value.startswith("MULTI_"):
        value = value[len("MULTI_") :]
    if value not in allowed:
        raise SystemExit(f"unsupported multi pattern: {value}")
    if recv_mode == "callback" and value not in {"SPOT", "STREAM"}:
        raise SystemExit(f"callback recv mode is unsupported for pattern MULTI_{value}")
    items.append(f"MULTI_{value}")

if not items:
    raise SystemExit("no valid multi pattern specified")

print(",".join(items))
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
    printf '%s' "10000"
  else
    printf '%s' "100"
  fi
}

wait_for_ready_endpoint() {
  local log_path="$1"
  python3 - "${log_path}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + 20.0
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
  python3 - "${log_path}" <<'PY'
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
deadline = time.time() + 20.0
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

extract_required_results() {
  local log_path="${1:-}"
  local pattern="${2:-}"
  local transport="${3:-}"
  local size="${4:-}"

  python3 - "${log_path}" "${pattern}" "${transport}" "${size}" <<'PY'
import csv
import sys

expected = sys.argv[2]
base = expected[len("MULTI_") :] if expected.startswith("MULTI_") else expected
required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
optional = [
    "server_cpu_pct",
    "server_mem_mb",
    "server_snd_pending_max",
    "server_rcv_pending_max",
    "server_rcv_pending_end",
]
found = {}
with open(sys.argv[1], encoding="utf-8", errors="replace") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7:
            continue
        if row[0] != "RESULT" or row[1] != "current":
            continue
        if row[2] not in {expected, base}:
            continue
        if row[3] != sys.argv[3] or row[4] != sys.argv[4]:
            continue
        if row[5] in required or row[5] in optional:
            row[2] = expected
            found[row[5]] = row

missing = [metric for metric in required if metric not in found]
if missing:
    raise SystemExit("missing required metrics: " + ",".join(missing))

for metric in required:
    print(",".join(found[metric]))
for metric in optional:
    if metric in found:
        print(",".join(found[metric]))
PY
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
optional = [
    "server_cpu_pct",
    "server_mem_mb",
    "server_snd_pending_max",
    "server_rcv_pending_max",
    "server_rcv_pending_end",
]
merged = {}

for path in (primary, secondary):
    if not path.exists():
        continue
    with path.open(encoding="utf-8", errors="replace") as handle:
        reader = csv.reader(handle)
        for row in reader:
            if len(row) != 7 or row[0] != "RESULT" or row[1] != "current":
                continue
            if row[2] not in {expected, base} or row[3] != transport or row[4] != size:
                continue
            metric = row[5]
            if metric in required or metric in optional:
                row[2] = expected
                merged[metric] = row

missing = [metric for metric in required if metric not in merged]
if missing:
    raise SystemExit("missing required metrics: " + ",".join(missing))

for metric in required:
    print(",".join(merged[metric]))
for metric in optional:
    if metric in merged:
        print(",".join(merged[metric]))
PY
}

emit_markdown_table() {
  local metrics_file="${1:-}"
  local pattern="${2:-}"
  local transport="${3:-}"
  local run_index="${4:-1}"

  if [[ ! -s "${metrics_file}" ]]; then
    return
  fi

  print_line "## PATTERN: ${pattern} (one-way)"
  print_line "  > Benchmarking current for ${pattern}..."
  print_line "    Testing ${transport}:"
  python3 - "${metrics_file}" <<'PY' | while IFS= read -r table_line; do
import csv
import sys
from collections import OrderedDict

required = [
    "throughput",
    "bandwidth",
    "latency",
    "latency_p95",
    "latency_p99",
    "server_cpu_pct",
    "server_mem_mb",
    "server_snd_pending_max",
    "server_rcv_pending_max",
    "server_rcv_pending_end",
]
rows = OrderedDict()
with open(sys.argv[1], encoding="utf-8") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7 or row[0] != "RESULT":
            continue
        size = row[4]
        metric = row[5]
        rows.setdefault(size, {})
        rows[size][metric] = row[6]

print("      | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB | Q.Snd.Max | Q.Rcv.Max | Q.Rcv.End |")
print("      |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|-----------|-----------|-----------|")
for size, metrics in rows.items():
    values = [metrics.get(metric, "NA") for metric in required]
    print(
        f"      | {size}B | {values[0]} | {values[1]} | {values[2]} | {values[3]} | {values[4]} | {values[5]} | {values[6]} | {values[7]} | {values[8]} | {values[9]} |"
    )
PY
    print_line "${table_line}"
  done
  print_line "    Testing ${transport}: Done"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern)
      PATTERN="${2:-}"
      shift
      ;;
    --transports)
      TRANSPORTS="${2:-}"
      shift
      ;;
    --msg-sizes)
      MSG_SIZES="${2:-}"
      shift
      ;;
    --recv)
      RECV_MODE="${2:-}"
      shift
      ;;
    --clients)
      CLIENTS="${2:-}"
      shift
      ;;
    --warmup)
      WARMUP="${2:-}"
      shift
      ;;
    --duration)
      DURATION="${2:-}"
      shift
      ;;
    --runs)
      RUNS="${2:-}"
      shift
      ;;
    --results-dir)
      RESULTS_ROOT="${2:-}"
      shift
      ;;
    --results-tag)
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

if [[ "${RECV_MODE}" != "recv" && "${RECV_MODE}" != "callback" ]]; then
  echo "--recv must be recv or callback." >&2
  exit 1
fi

validate_uint "--warmup" "${WARMUP}"
validate_uint "--duration" "${DURATION}"
validate_uint "--runs" "${RUNS}"
validate_uint "PERF_MULTI_CONNECT_READY_TIMEOUT_MS" "${READY_TIMEOUT_MS}"

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

PATTERN="$(normalize_multi_pattern_csv "${PATTERN}" "${RECV_MODE}")"

mkdir -p "${RESULTS_ROOT}/multi/tmp" "${RESULTS_ROOT}/multi/report" \
  "${RESULTS_ROOT}/multi/baseline"

platform="$(normalize_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report_base="perf_${platform}_${RECV_MODE}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report_base="${report_base}_${RESULTS_TAG}"
fi
REPORT="${RESULTS_ROOT}/multi/report/${report_base}.txt"
: > "${REPORT}"

print_line "## Effective Options (start)"
print_line "- runs: ${RUNS}"
print_line "- recv_mode: ${RECV_MODE}"
print_line "- duration_seconds: ${DURATION}"
print_line "- warmup_seconds: ${WARMUP}"
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS}"
print_line "- msg_sizes: ${MSG_SIZES:-default(pattern)}"
print_line "- clients: ${CLIENTS:-default(pattern)}"
print_line ""

IFS=',' read -r -a patterns <<< "${PATTERN}"
IFS=',' read -r -a transports <<< "${TRANSPORTS}"

status=0
result_lines=0
for (( run_index=1; run_index<=RUNS; run_index++ )); do
  for pattern in "${patterns[@]}"; do
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
    for transport in "${transports[@]}"; do
      transport="${transport//[[:space:]]/}"
      [[ -n "${transport}" ]] || continue
      if [[ "${transport}" == "inproc" ]]; then
        echo "multi suite does not support inproc transport." >&2
        status=1
        continue
      fi

      metrics_file="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_run${run_index}.metrics"
      : > "${metrics_file}"

      for size in "${msg_sizes[@]}"; do
        size="${size//[[:space:]]/}"
        [[ -n "${size}" ]] || continue

        server_log="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_server_run${run_index}.log"
        client_log="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_client_run${run_index}.log"
        control_fifo="${RESULTS_ROOT}/multi/tmp/${pattern,,}_${transport}_${size}_run${run_index}.ctl"
        rm -f "${server_log}" "${client_log}" "${control_fifo}"

        control_fd=''
        server_endpoint=''
        if [[ "${pattern}" == "MULTI_SPOT" ]]; then
          mkfifo "${control_fifo}"
          exec {control_fd}<>"${control_fifo}"
          rm -f "${control_fifo}"
          PERF_CLIENTS="${pattern_clients}" PERF_MULTI_CLIENTS="${pattern_clients}" \
            PERF_WARMUP_SECONDS="${WARMUP}" PERF_MULTI_WARMUP_SECONDS="${WARMUP}" \
            PERF_DURATION_SECONDS="${DURATION}" PERF_MULTI_DURATION_SECONDS="${DURATION}" \
            PERF_CONNECT_READY_TIMEOUT_MS="${READY_TIMEOUT_MS}" \
            dotnet run -c "${CONFIGURATION}" --no-restore --project "${PROJECT}" -- \
            --multi-server "${pattern}" "${transport}" "${size}" \
            --recv "${RECV_MODE}" <&${control_fd} > "${server_log}" 2>&1 &
        else
          PERF_CLIENTS="${pattern_clients}" PERF_MULTI_CLIENTS="${pattern_clients}" \
            PERF_WARMUP_SECONDS="${WARMUP}" PERF_MULTI_WARMUP_SECONDS="${WARMUP}" \
            PERF_DURATION_SECONDS="${DURATION}" PERF_MULTI_DURATION_SECONDS="${DURATION}" \
            PERF_CONNECT_READY_TIMEOUT_MS="${READY_TIMEOUT_MS}" \
            dotnet run -c "${CONFIGURATION}" --no-restore --project "${PROJECT}" -- \
            --multi-server "${pattern}" "${transport}" "${size}" \
            --recv "${RECV_MODE}" > "${server_log}" 2>&1 &
        fi
        server_pid=$!

        echo "RUN pattern=${pattern} transport=${transport} size=${size} recv=${RECV_MODE} clients=${pattern_clients} run=${run_index}"

        if ! server_endpoint="$(wait_for_ready_endpoint "${server_log}")"; then
          cat "${server_log}" >&2 || true
          echo "server did not become ready for ${pattern} ${transport} ${size}" >&2
          terminate_pid "${server_pid}"
          if [[ -n "${control_fd}" ]]; then
            exec {control_fd}>&-
          fi
          status=1
          continue
        fi

        if [[ "${pattern}" == "MULTI_STREAM" ]]; then
          if [[ ! -x "${STREAM_CLIENT}" ]]; then
            echo "STREAM client binary not found: ${STREAM_CLIENT}" >&2
            terminate_pid "${server_pid}"
            status=1
            continue
          fi

          if PERF_CLIENTS="${pattern_clients}" PERF_MULTI_CLIENTS="${pattern_clients}" \
            PERF_WARMUP_SECONDS="${WARMUP}" PERF_MULTI_WARMUP_SECONDS="${WARMUP}" \
            PERF_DURATION_SECONDS="${DURATION}" PERF_MULTI_DURATION_SECONDS="${DURATION}" \
            PERF_CONNECT_READY_TIMEOUT_MS="${READY_TIMEOUT_MS}" \
            "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM \
            --sizes "${size}" --runs 1 --warmup "${WARMUP}" --duration "${DURATION}" \
            --ccu "${pattern_clients}" --print-perf-result 2 --send-stop-token 1 \
            --endpoint "${server_endpoint}" > "${client_log}" 2>&1; then
            if ! wait_for_pid "${server_pid}" 5; then
              terminate_pid "${server_pid}"
            else
              wait "${server_pid}" || true
            fi
            extracted="$(extract_results_from_logs "${client_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"
          else
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${server_pid}"
            status=1
            continue
          fi
        elif [[ "${pattern}" == "MULTI_SPOT" ]]; then
          PERF_CLIENTS="${pattern_clients}" PERF_MULTI_CLIENTS="${pattern_clients}" \
            PERF_WARMUP_SECONDS="${WARMUP}" PERF_MULTI_WARMUP_SECONDS="${WARMUP}" \
            PERF_DURATION_SECONDS="${DURATION}" PERF_MULTI_DURATION_SECONDS="${DURATION}" \
            PERF_CONNECT_READY_TIMEOUT_MS="${READY_TIMEOUT_MS}" \
            dotnet run -c "${CONFIGURATION}" --no-restore --project "${PROJECT}" -- \
            --multi-client "${pattern}" "${transport}" "${size}" \
            --recv "${RECV_MODE}" --endpoint "${server_endpoint}" > "${client_log}" 2>&1 &
          client_pid=$!

          if ! wait_for_client_ready_line "${client_log}"; then
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${client_pid}"
            printf 'STOP\n' >&${control_fd} || true
            terminate_pid "${server_pid}"
            exec {control_fd}>&-
            status=1
            continue
          fi

          printf 'START,%s\n' "${size}" >&${control_fd}
          if wait "${client_pid}"; then
            printf 'STOP\n' >&${control_fd} || true
            if ! wait_for_pid "${server_pid}" 5; then
              terminate_pid "${server_pid}"
            else
              wait "${server_pid}" || true
            fi
            extracted="$(extract_results_from_logs "${client_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"
          else
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            printf 'STOP\n' >&${control_fd} || true
            terminate_pid "${server_pid}"
            exec {control_fd}>&-
            status=1
            continue
          fi
          exec {control_fd}>&-
        else
          if PERF_CLIENTS="${pattern_clients}" PERF_MULTI_CLIENTS="${pattern_clients}" \
            PERF_WARMUP_SECONDS="${WARMUP}" PERF_MULTI_WARMUP_SECONDS="${WARMUP}" \
            PERF_DURATION_SECONDS="${DURATION}" PERF_MULTI_DURATION_SECONDS="${DURATION}" \
            PERF_CONNECT_READY_TIMEOUT_MS="${READY_TIMEOUT_MS}" \
            dotnet run -c "${CONFIGURATION}" --no-restore --project "${PROJECT}" -- \
            --multi-client "${pattern}" "${transport}" "${size}" \
            --recv "${RECV_MODE}" --endpoint "${server_endpoint}" > "${client_log}" 2>&1; then
            if ! wait_for_pid "${server_pid}" 5; then
              terminate_pid "${server_pid}"
            else
              wait "${server_pid}" || true
            fi
            metric_log="${server_log}"
            if [[ "${pattern}" == "MULTI_PUBSUB" || "${pattern}" == "MULTI_ROUTER_ROUTER" ]]; then
              metric_log="${client_log}"
            fi
            extracted="$(extract_results_from_logs "${metric_log}" "${server_log}" "${pattern}" "${transport}" "${size}")"
          else
            cat "${server_log}" >&2 || true
            cat "${client_log}" >&2 || true
            terminate_pid "${server_pid}"
            status=1
            continue
          fi
        fi

        while IFS= read -r result_line; do
          [[ -n "${result_line}" ]] || continue
          print_line "${result_line}"
          printf '%s\n' "${result_line}" >> "${metrics_file}"
          result_lines=$((result_lines + 1))
        done <<< "${extracted}"
      done

      if [[ -s "${metrics_file}" ]]; then
        print_line ""
        emit_markdown_table "${metrics_file}" "${pattern}" "${transport}" "${run_index}"
        print_line ""
      fi
    done
  done
done

print_line "## Effective Options (result)"
print_line "- recv_mode: ${RECV_MODE}"
print_line "- result_lines: ${result_lines}"
print_line "- status: $( [[ "${status}" -eq 0 ]] && printf 'complete' || printf 'failed' )"

echo "saved report: ${REPORT}"
exit "${status}"
