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
MSG_SIZES="${PERF_MSG_SIZES:-64,1024}"
RECV_MODE="recv"
CLIENTS="${PERF_MULTI_CLIENTS:-32}"
WARMUP="${PERF_MULTI_WARMUP_SECONDS:-2}"
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
RESULTS_TAG=""

usage() {
  cat <<'USAGE'
Usage: perf/multi/run_benchmarks.sh [options]

Options:
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --recv MODE            recv|callback.
  --clients N            Client count.
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
    --clients) CLIENTS="${2:-}"; shift ;;
    --warmup) WARMUP="${2:-}"; shift ;;
    --duration) DURATION="${2:-}"; shift ;;
    --results-dir) RESULTS_ROOT="${2:-}"; shift ;;
    --results-tag) RESULTS_TAG="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

mkdir -p "${RESULTS_ROOT}/multi/tmp" "${RESULTS_ROOT}/multi/report" "${RESULTS_ROOT}/multi/baseline"
"${ROOT_DIR}/gradlew" :perf-multi:installDist >/dev/null

if [[ "${PATTERN}" == "ALL" ]]; then
  if [[ "${RECV_MODE}" == "callback" ]]; then
    PATTERN="MULTI_SPOT,MULTI_STREAM"
  else
    PATTERN="MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_STREAM"
  fi
fi

platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
timestamp="$(date +%Y%m%d_%H%M%S)"
report="${RESULTS_ROOT}/multi/report/perf_${platform}_${RECV_MODE}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

echo "# java perf multi" > "${report}"
echo "# pattern=${PATTERN} recv=${RECV_MODE} msg_sizes=${MSG_SIZES} clients=${CLIENTS}" >> "${report}"

pick_endpoint() {
  local transport="$1"
  local token="$2"
  if [[ "${transport}" == "ipc" ]]; then
    echo "ipc://${ROOT_DIR}/perf/results/multi/tmp/${token}-${RANDOM}.sock"
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

pick_control_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
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
            sock.close()
            raise SystemExit(0)
    finally:
        sock.close()
    time.sleep(0.05)
raise SystemExit(1)
PY
}

IFS=',' read -r -a patterns <<< "${PATTERN}"
IFS=',' read -r -a msg_sizes <<< "${MSG_SIZES}"
IFS=',' read -r -a transports <<< "${TRANSPORTS}"
for pattern in "${patterns[@]}"; do
  bare_pattern="${pattern#MULTI_}"
  if [[ "${RECV_MODE}" == "callback" && "${bare_pattern}" != "SPOT" && "${bare_pattern}" != "STREAM" ]]; then
    echo "${pattern} callback skipped by policy" | tee -a "${report}"
    continue
  fi
  for transport in "${transports[@]}"; do
    for size in "${msg_sizes[@]}"; do
      if [[ "${bare_pattern}" == "STREAM" ]]; then
        fifo="${RESULTS_ROOT}/multi/tmp/stream_control_${transport}_${size}.fifo"
        rm -f "${fifo}"
        mkfifo "${fifo}"
        server_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_server.log"
        client_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_client.log"
        endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
        exec 3<>"${fifo}"
        "${RUNNER}" --multi-server "${pattern}" "${transport}" "${size}" \
          --recv "${RECV_MODE}" --endpoint "${endpoint}" --clients "${CLIENTS}" \
          --warmup "${WARMUP}" --duration "${DURATION}" --control-port 0 \
          <"${fifo}" >"${server_log}" 2>&1 &
        server_pid=$!
        wait_for_tcp_endpoint "${endpoint}"
        "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM \
          --sizes "${size}" --runs 1 --warmup "${WARMUP}" \
          --duration "${DURATION}" --ccu "${CLIENTS}" \
          --print-perf-result 2 --send-stop-token 1 --endpoint "${endpoint}" \
          >"${client_log}" 2>&1
        printf 'STOP\n' >&3
        exec 3>&-
        wait "${server_pid}"
        rm -f "${fifo}"
        throughput="$(awk -F, '/^RESULT,current,STREAM,/ && $6=="throughput" {print $7}' "${client_log}" | tail -n 1)"
        bandwidth="$(awk -F, '/^RESULT,current,STREAM,/ && $6=="bandwidth" {print $7}' "${client_log}" | tail -n 1)"
        lat_mean="$(awk -F, '/^RESULT,current,STREAM,/ && $6=="latency" {print $7}' "${client_log}" | tail -n 1)"
        lat_p95="$(awk -F, '/^RESULT,current,STREAM,/ && $6=="latency_p95" {print $7}' "${client_log}" | tail -n 1)"
        lat_p99="$(awk -F, '/^RESULT,current,STREAM,/ && $6=="latency_p99" {print $7}' "${client_log}" | tail -n 1)"
        line="RESULT status=ok reason=- throughput=${throughput:-0.00} bandwidth=${bandwidth:-0.00} lat_mean=${lat_mean:-0.00} lat_p95=${lat_p95:-0.00} lat_p99=${lat_p99:-0.00} lat_unit=ms cpu_pct=N/A mem_mb=N/A"
        echo "${pattern} ${transport} ${size} ${line}" | tee -a "${report}"
        continue
      fi
      endpoint="$(pick_endpoint "${transport}" "${bare_pattern}")"
      control_port="$(pick_control_port)"
      server_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_server.log"
      client_log="${RESULTS_ROOT}/multi/tmp/${bare_pattern,,}_${transport}_${size}_client.log"
      echo "RUN pattern=${pattern} transport=${transport} size=${size} recv=${RECV_MODE}"
      "${RUNNER}" --multi-server "${pattern}" "${transport}" "${size}" \
        --recv "${RECV_MODE}" --endpoint "${endpoint}" --clients "${CLIENTS}" \
        --warmup "${WARMUP}" --duration "${DURATION}" --control-port "${control_port}" \
        >"${server_log}" 2>&1 &
      server_pid=$!
      "${RUNNER}" --multi-client "${pattern}" "${transport}" "${size}" \
        --recv "${RECV_MODE}" --endpoint "${endpoint}" --clients "${CLIENTS}" \
        --warmup "${WARMUP}" --duration "${DURATION}" --control-port "${control_port}" \
        >"${client_log}" 2>&1
      wait "${server_pid}"
      metric_log="${server_log}"
      if [[ "${bare_pattern}" == "PUBSUB" || "${bare_pattern}" == "SPOT" ]]; then
        metric_log="${client_log}"
      fi
      line="$(grep '^RESULT ' "${metric_log}" | tail -n 1)"
      echo "${pattern} ${transport} ${size} ${line}" | tee -a "${report}"
    done
  done
done

echo "saved report: ${report}"
