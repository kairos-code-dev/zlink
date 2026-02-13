#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../../" && pwd)"
PLAYHOUSE_DIR="${PLAYHOUSE_DIR:-${ROOT_DIR}/../playhouse}"

TIMESTAMP="${TIMESTAMP:-$(date +%Y%m%d_%H%M%S)}"
OUT_DIR="${OUT_DIR:-${PLAYHOUSE_DIR}/doc/plan/zlink-migration/results/${TIMESTAMP}/libzlink-stream-10k}"

METRICS_CSV="${OUT_DIR}/metrics.csv"
SCENARIO_LOG="${OUT_DIR}/scenario.log"
SUMMARY_JSON="${OUT_DIR}/summary.json"
KERNEL_LOG="${OUT_DIR}/kernel.log"

mkdir -p "${OUT_DIR}"
: >"${SCENARIO_LOG}"
rm -f "${METRICS_CSV}"

CCU="${CCU:-10000}"
SIZE="${SIZE:-1024}"
INFLIGHT="${INFLIGHT:-30}"
WARMUP="${WARMUP:-3}"
MEASURE="${MEASURE:-10}"
DRAIN_TIMEOUT="${DRAIN_TIMEOUT:-10}"
CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY:-256}"
BACKLOG="${BACKLOG:-32768}"
HWM="${HWM:-1000000}"
IO_THREADS="${IO_THREADS:-1}"
SEND_BATCH="${SEND_BATCH:-1}"
LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE:-1}"

RUN_S3="${RUN_S3:-0}"
RUN_S4="${RUN_S4:-0}"
RUN_DOTNET="${RUN_DOTNET:-1}"
RUN_CPPSERVER="${RUN_CPPSERVER:-1}"
RUN_NET_ZLINK="${RUN_NET_ZLINK:-1}"

overall_rc=0

{
  echo "=== STREAM Scenario Compare ==="
  echo "timestamp=${TIMESTAMP}"
  echo "out_dir=${OUT_DIR}"
  echo "ccu=${CCU} size=${SIZE} inflight=${INFLIGHT}"
  echo "warmup=${WARMUP}s measure=${MEASURE}s drain_timeout=${DRAIN_TIMEOUT}s"
  echo "connect_concurrency=${CONNECT_CONCURRENCY} backlog=${BACKLOG} hwm=${HWM}"
  echo "io_threads=${IO_THREADS} send_batch=${SEND_BATCH} latency_sample_rate=${LATENCY_SAMPLE_RATE}"
  echo "run_s3=${RUN_S3} run_s4=${RUN_S4}"
  echo "run_dotnet=${RUN_DOTNET} run_cppserver=${RUN_CPPSERVER} run_net_zlink=${RUN_NET_ZLINK}"
} | tee -a "${SCENARIO_LOG}"

if ! CCU="${CCU}" \
    SIZE="${SIZE}" \
    INFLIGHT="${INFLIGHT}" \
    WARMUP="${WARMUP}" \
    MEASURE="${MEASURE}" \
    DRAIN_TIMEOUT="${DRAIN_TIMEOUT}" \
    CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}" \
    BACKLOG="${BACKLOG}" \
    HWM="${HWM}" \
    IO_THREADS="${IO_THREADS}" \
    SEND_BATCH="${SEND_BATCH}" \
    LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE}" \
    RUN_S3="${RUN_S3}" \
    RUN_S4="${RUN_S4}" \
    BASE_PORT="${ZLINK_BASE_PORT:-27110}" \
    SCENARIO_PREFIX="zlink-" \
    METRICS_CSV="${METRICS_CSV}" \
    LOG_FILE="${SCENARIO_LOG}" \
    "${SCRIPT_DIR}/zlink/run_stream_scenarios.sh"; then
  overall_rc=1
fi

if ! CCU="${CCU}" \
    SIZE="${SIZE}" \
    INFLIGHT="${INFLIGHT}" \
    WARMUP="${WARMUP}" \
    MEASURE="${MEASURE}" \
    DRAIN_TIMEOUT="${DRAIN_TIMEOUT}" \
    CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}" \
    BACKLOG="${BACKLOG}" \
    IO_THREADS="${IO_THREADS}" \
    LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE}" \
    RUN_S3="${RUN_S3}" \
    RUN_S4="${RUN_S4}" \
    BASE_PORT="${ASIO_BASE_PORT:-27210}" \
    SCENARIO_PREFIX="asio-" \
    METRICS_CSV="${METRICS_CSV}" \
    LOG_FILE="${SCENARIO_LOG}" \
    "${SCRIPT_DIR}/asio/run_stream_scenarios.sh"; then
  overall_rc=1
fi

if [[ "${RUN_DOTNET}" == "1" ]]; then
  if ! CCU="${CCU}" \
      SIZE="${SIZE}" \
      INFLIGHT="${INFLIGHT}" \
      WARMUP="${WARMUP}" \
      MEASURE="${MEASURE}" \
      DRAIN_TIMEOUT="${DRAIN_TIMEOUT}" \
      CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}" \
      BACKLOG="${BACKLOG}" \
      IO_THREADS="${IO_THREADS}" \
      LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE}" \
      BASE_PORT="${DOTNET_BASE_PORT:-27310}" \
      SCENARIO_PREFIX="dotnet-" \
      METRICS_CSV="${METRICS_CSV}" \
      LOG_FILE="${SCENARIO_LOG}" \
      "${SCRIPT_DIR}/dotnet/run_stream_scenarios.sh"; then
    overall_rc=1
  fi
fi

if [[ "${RUN_CPPSERVER}" == "1" ]]; then
  if ! CCU="${CCU}" \
      SIZE="${SIZE}" \
      INFLIGHT="${INFLIGHT}" \
      WARMUP="${WARMUP}" \
      MEASURE="${MEASURE}" \
      DRAIN_TIMEOUT="${DRAIN_TIMEOUT}" \
      CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}" \
      BACKLOG="${BACKLOG}" \
      IO_THREADS="${IO_THREADS}" \
      LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE}" \
      BASE_PORT="${CPPSERVER_BASE_PORT:-27410}" \
      SCENARIO_PREFIX="cppserver-" \
      METRICS_CSV="${METRICS_CSV}" \
      LOG_FILE="${SCENARIO_LOG}" \
      "${SCRIPT_DIR}/cppserver/run_stream_scenarios.sh"; then
    overall_rc=1
  fi
fi

if [[ "${RUN_NET_ZLINK}" == "1" ]]; then
  if ! CCU="${CCU}" \
      SIZE="${SIZE}" \
      INFLIGHT="${INFLIGHT}" \
      WARMUP="${WARMUP}" \
      MEASURE="${MEASURE}" \
      DRAIN_TIMEOUT="${DRAIN_TIMEOUT}" \
      CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}" \
      BACKLOG="${BACKLOG}" \
      HWM="${HWM}" \
      IO_THREADS="${IO_THREADS}" \
      LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE}" \
      BASE_PORT="${NET_ZLINK_BASE_PORT:-27510}" \
      SCENARIO_PREFIX="net-zlink-" \
      METRICS_CSV="${METRICS_CSV}" \
      LOG_FILE="${SCENARIO_LOG}" \
      "${SCRIPT_DIR}/net-zlink/run_stream_scenarios.sh"; then
    overall_rc=1
  fi
fi

if dmesg >"${KERNEL_LOG}" 2>&1; then
  tail -n 400 "${KERNEL_LOG}" >"${KERNEL_LOG}.tail"
  mv "${KERNEL_LOG}.tail" "${KERNEL_LOG}"
else
  echo "dmesg unavailable (permission or platform restriction)" >"${KERNEL_LOG}"
fi

total_rows=$(awk 'NR>1{c++} END{print c+0}' "${METRICS_CSV}" 2>/dev/null || echo 0)
pass_rows=$(awk -F, 'NR>1 && $17=="PASS"{c++} END{print c+0}' "${METRICS_CSV}" 2>/dev/null || echo 0)
skip_rows=$(awk -F, 'NR>1 && $17=="SKIP"{c++} END{print c+0}' "${METRICS_CSV}" 2>/dev/null || echo 0)
fail_rows=$(awk -F, 'NR>1 && $17=="FAIL"{c++} END{print c+0}' "${METRICS_CSV}" 2>/dev/null || echo 0)

cat >"${SUMMARY_JSON}" <<JSON
{
  "timestamp": "${TIMESTAMP}",
  "ccu": ${CCU},
  "size": ${SIZE},
  "inflight": ${INFLIGHT},
  "warmup_sec": ${WARMUP},
  "measure_sec": ${MEASURE},
  "drain_timeout_sec": ${DRAIN_TIMEOUT},
  "connect_concurrency": ${CONNECT_CONCURRENCY},
  "backlog": ${BACKLOG},
  "hwm": ${HWM},
  "run_s3": ${RUN_S3},
  "run_s4": ${RUN_S4},
  "run_dotnet": ${RUN_DOTNET},
  "run_cppserver": ${RUN_CPPSERVER},
  "run_net_zlink": ${RUN_NET_ZLINK},
  "total_rows": ${total_rows},
  "pass_rows": ${pass_rows},
  "skip_rows": ${skip_rows},
  "fail_rows": ${fail_rows},
  "metrics_csv": "${METRICS_CSV}",
  "scenario_log": "${SCENARIO_LOG}",
  "kernel_log": "${KERNEL_LOG}",
  "overall_rc": ${overall_rc}
}
JSON

{
  echo
  echo "=== Output ==="
  echo "summary.json: ${SUMMARY_JSON}"
  echo "scenario.log: ${SCENARIO_LOG}"
  echo "metrics.csv: ${METRICS_CSV}"
  echo "kernel.log: ${KERNEL_LOG}"
} | tee -a "${SCENARIO_LOG}"

exit "${overall_rc}"
