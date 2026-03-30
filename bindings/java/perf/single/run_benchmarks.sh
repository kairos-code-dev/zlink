#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUNNER="${ROOT_DIR}/perf/single/Zlink.BindingBench/build/install/zlink-java-perf-single/bin/zlink-java-perf-single"
RESULTS_ROOT="${ROOT_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS=""
MSG_SIZES="${PERF_MSG_SIZES:-64,1024}"
RECV_MODE="callback"
WARMUP="${PERF_SINGLE_WARMUP_SECONDS:-2}"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
RESULTS_TAG=""

usage() {
  cat <<'USAGE'
Usage: perf/single/run_benchmarks.sh [options]

Options:
  --pattern NAME         Pattern list or ALL.
  --transports LIST      Transport list override.
  --msg-sizes LIST       Payload sizes.
  --recv MODE            callback only.
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

mkdir -p "${RESULTS_ROOT}/single/tmp" "${RESULTS_ROOT}/single/report" "${RESULTS_ROOT}/single/baseline"
"${ROOT_DIR}/gradlew" :perf-single:installDist >/dev/null

if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERN="PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT"
fi

platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
timestamp="$(date +%Y%m%d_%H%M%S)"
report="${RESULTS_ROOT}/single/report/perf_${platform}_${RECV_MODE}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report="${report}_${RESULTS_TAG}"
fi
report="${report}.txt"

echo "# java perf single" > "${report}"
echo "# pattern=${PATTERN} recv=${RECV_MODE} msg_sizes=${MSG_SIZES}" >> "${report}"

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

IFS=',' read -r -a patterns <<< "${PATTERN}"
IFS=',' read -r -a msg_sizes <<< "${MSG_SIZES}"
for pattern in "${patterns[@]}"; do
  pattern="${pattern#MULTI_}"
  current_transports="${TRANSPORTS:-$(default_transports "${pattern}")}"
  IFS=',' read -r -a transports <<< "${current_transports}"
  for transport in "${transports[@]}"; do
    for size in "${msg_sizes[@]}"; do
      echo "RUN pattern=${pattern} transport=${transport} size=${size}"
      line="$("${RUNNER}" "${pattern}" "${transport}" "${size}" --warmup "${WARMUP}" --duration "${DURATION}" --recv "${RECV_MODE}" | tail -n 1)"
      echo "${pattern} ${transport} ${size} ${line}" | tee -a "${report}"
    done
  done
done

echo "saved report: ${report}"
