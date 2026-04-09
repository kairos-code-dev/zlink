#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

PATTERN="ALL"
DURATION="5"
WARMUP="2"
MSG_SIZES="64,256,1024,65536,131072,262144"
TRANSPORTS=""
RUNS="1"
RESULTS_DIR="${SCRIPT_DIR}/results/single/report"
RESULTS_TAG=""
OUTPUT_FILE=""

usage() {
  cat <<'USAGE'
Usage: bindings/go/perf/run_benchmarks.sh [options]

Options:
  --pattern NAME
  --duration N
  --warmup N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --results-dir PATH
  --results-tag NAME
  --output PATH
  --build-dir PATH
  --reuse-build
  --clean-build
  --pin-cpu
  --io-threads N
  --hwm N
  --send-hwm N
  --recv-hwm N
  --sndbuf SIZE
  --rcvbuf SIZE
  --sndtimeo N
  --rcvtimeo N
  --send-timeout-ms N
  --recv-timeout-ms N
  -h, --help

Notes:
  - Supported single patterns: PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
    --transports) TRANSPORTS="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --results-tag) RESULTS_TAG="$2"; shift 2 ;;
    --output) OUTPUT_FILE="$2"; shift 2 ;;
    --build-dir|--io-threads|--hwm|--send-hwm|--recv-hwm|--sndbuf|--rcvbuf|--sndtimeo|--rcvtimeo|--send-timeout-ms|--recv-timeout-ms)
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
RESULTS_FILE="${RESULTS_DIR}/perf_go_single_${PLATFORM}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"

IFS=',' read -r -a SIZES <<< "${MSG_SIZES}"
if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERNS=("PAIR" "PUBSUB" "DEALER_DEALER" "DEALER_ROUTER" "ROUTER_ROUTER" "SPOT")
else
  IFS=',' read -r -a PATTERNS <<< "${PATTERN}"
fi

if [[ -n "${TRANSPORTS}" ]]; then
  IFS=',' read -r -a XPORTS_FILTER <<< "${TRANSPORTS}"
else
  XPORTS_FILTER=()
fi

pattern_transports() {
  case "$1" in
    SPOT) echo "tcp tls ws wss" ;;
    *)
      if [[ "${PLATFORM}" == "windows" ]]; then
        echo "tcp tls ws wss inproc"
      else
        echo "tcp tls ws wss inproc ipc"
      fi
      ;;
  esac
}

transport_enabled() {
  local transport="$1"
  if [[ "${#XPORTS_FILTER[@]}" -eq 0 ]]; then
    return 0
  fi
  local candidate
  for candidate in "${XPORTS_FILTER[@]}"; do
    if [[ "${candidate}" == "${transport}" ]]; then
      return 0
    fi
  done
  return 1
}

{
  echo "## Effective Options (start)"
  echo "- lang: go"
  echo "- suite: single"
  echo "- pattern: ${PATTERN}"
  echo "- duration: ${DURATION}s"
  echo "- warmup: ${WARMUP}s"
  echo "- msg_sizes: ${MSG_SIZES}"
  echo "- transports: ${TRANSPORTS:-auto}"
  echo "- runs: ${RUNS}"
  echo "## Effective Options (end)"
  echo

  for run in $(seq 1 "${RUNS}"); do
    for pattern in "${PATTERNS[@]}"; do
      read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
      for transport in "${PATTERN_XPORTS[@]}"; do
        if ! transport_enabled "${transport}"; then
          continue
        fi
        for size in "${SIZES[@]}"; do
          go run ./perf/single \
            --pattern "${pattern}" \
            --transport "${transport}" \
            --msg-size "${size}" \
            --duration "${DURATION}"
        done
      done
    done
  done
} > "${RESULTS_FILE}"

if [[ -n "${OUTPUT_FILE}" ]]; then
  cp "${RESULTS_FILE}" "${OUTPUT_FILE}"
fi

cat "${RESULTS_FILE}"

echo "Results saved to: ${RESULTS_FILE}"
