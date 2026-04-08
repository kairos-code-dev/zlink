#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

PATTERN="ALL"
RECV_MODE="recv"
DURATION="5"
WARMUP="2"
MSG_SIZES="64,256,1024,65536,131072,262144"
TRANSPORTS="tcp"
RUNS="1"
CLIENTS=""
RESULTS_DIR="${SCRIPT_DIR}/results/multi/report"
RESULTS_TAG=""
OUTPUT_FILE=""

usage() {
  cat <<'USAGE'
Usage: bindings/go/perf/run_benchmarks_multi.sh [options]

Options:
  --pattern NAME
  --recv MODE
  --duration N
  --warmup N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --clients N
  --results-dir PATH
  --results-tag NAME
  --output PATH
  --build-dir PATH
  --reuse-build
  --clean-build
  --pin-cpu
  --io-threads N
  --server-io-threads N
  --client-io-threads N
  --hwm N
  --send-hwm N
  --recv-hwm N
  --sndbuf SIZE
  --rcvbuf SIZE
  --sndtimeo N
  --rcvtimeo N
  --send-timeout-ms N
  --recv-timeout-ms N
  --connect-concurrency N
  --transport-transition-ms N
  --pattern-transition-ms N
  --server-ready-timeout-ms N
  --connect-ready-timeout-ms N
  --monitor-hwm N
  --server-shutdown-timeout-ms N
  --server-bind-port N
  -h, --help

Notes:
  - Go perf currently supports tcp transport only.
  - Supported multi patterns: MULTI_PUBSUB,MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_SPOT,MULTI_STREAM
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --recv) RECV_MODE="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
    --transports) TRANSPORTS="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --clients) CLIENTS="$2"; shift 2 ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --results-tag) RESULTS_TAG="$2"; shift 2 ;;
    --output) OUTPUT_FILE="$2"; shift 2 ;;
    --build-dir|--io-threads|--server-io-threads|--client-io-threads|--hwm|--send-hwm|--recv-hwm|--sndbuf|--rcvbuf|--sndtimeo|--rcvtimeo|--send-timeout-ms|--recv-timeout-ms|--connect-concurrency|--transport-transition-ms|--pattern-transition-ms|--server-ready-timeout-ms|--connect-ready-timeout-ms|--monitor-hwm|--server-shutdown-timeout-ms|--server-bind-port)
      shift 2 ;;
    --reuse-build|--clean-build|--pin-cpu)
      shift ;;
    *)
      echo "Error: unknown option $1" >&2
      exit 1 ;;
  esac
done

case "$(uname -s)" in
  Linux*) PLATFORM="linux" ;;
  Darwin*) PLATFORM="macos" ;;
  *) PLATFORM="windows" ;;
esac

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""
if [[ -n "${RESULTS_TAG}" ]]; then
  TAG_SUFFIX="_${RESULTS_TAG}"
fi
RESULTS_FILE="${RESULTS_DIR}/perf_${PLATFORM}_${RECV_MODE}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"

IFS=',' read -r -a SIZES <<< "${MSG_SIZES}"
IFS=',' read -r -a XPORTS <<< "${TRANSPORTS}"
if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERNS=("MULTI_PUBSUB" "MULTI_DEALER_DEALER" "MULTI_DEALER_ROUTER" "MULTI_ROUTER_ROUTER" "MULTI_SPOT" "MULTI_STREAM")
else
  IFS=',' read -r -a PATTERNS <<< "${PATTERN}"
fi

tee_args=("${RESULTS_FILE}")
if [[ -n "${OUTPUT_FILE}" ]]; then
  tee_args+=("${OUTPUT_FILE}")
fi

{
  echo "## Effective Options (start)"
  echo "  pattern:   ${PATTERN}"
  echo "  recv:      ${RECV_MODE}"
  echo "  duration:  ${DURATION}s"
  echo "  warmup:    ${WARMUP}s"
  echo "  msg_sizes: ${MSG_SIZES}"
  echo "  transports: ${TRANSPORTS}"
  if [[ -n "${CLIENTS}" ]]; then
    echo "  clients:   ${CLIENTS}"
  else
    echo "  clients:   auto (default=100, stream=10000)"
  fi
  echo "  runs:      ${RUNS}"
  echo "## Effective Options (end)"
  echo

  for run in $(seq 1 "${RUNS}"); do
    for pattern in "${PATTERNS[@]}"; do
      resolved_clients="${CLIENTS}"
      if [[ -z "${resolved_clients}" ]]; then
        if [[ "${pattern}" == "MULTI_STREAM" ]]; then
          resolved_clients="10000"
        else
          resolved_clients="100"
        fi
      fi
      for transport in "${XPORTS[@]}"; do
        for size in "${SIZES[@]}"; do
          go run ./perf/multi \
            --pattern "${pattern}" \
            --transport "${transport}" \
            --msg-size "${size}" \
            --duration "${DURATION}" \
            --recv "${RECV_MODE}" \
            --clients "${resolved_clients}"
        done
      done
    done
  done
} | tee "${tee_args[@]}"

echo "Results saved to: ${RESULTS_FILE}"
