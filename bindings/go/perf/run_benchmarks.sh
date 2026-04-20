#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

export GOCACHE="${GOCACHE:-/tmp/zlink-go-cache}"
export GOTMPDIR="${GOTMPDIR:-/tmp/zlink-go-tmp}"
mkdir -p "${GOCACHE}" "${GOTMPDIR}"

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
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-go-single.XXXXXX")"
cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

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

is_unsupported_output() {
  local output_file="$1"
  grep -Eiq '(protocol not supported|operation not permitted|permission denied|address already in use|listen eperm|eaddrinuse|eacces|errno=1|errno=98)' "${output_file}"
}

pattern_transport_supported() {
  local pattern="$1"
  local transport="$2"
  case "${pattern}/${transport}" in
    DEALER_ROUTER/inproc|ROUTER_ROUTER/inproc)
      return 1
      ;;
    *)
      return 0
      ;;
  esac
}

append_case_output() {
  local case_log="$1"
  cat "${case_log}" >> "${RESULTS_FILE}"
  if [[ -s "${case_log}" ]]; then
    printf '\n' >> "${RESULTS_FILE}"
  fi
}

count_result_lines() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local case_log="$4"
  awk -F',' -v pattern="${pattern}" -v transport="${transport}" -v size="${size}" '
    $1 == "RESULT" && $3 == pattern && $4 == transport && $5 == size { count++ }
    END { print count + 0 }
  ' "${case_log}"
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
  echo
} > "${RESULTS_FILE}"

result_lines=0
unsupported=0
fail=0

for run in $(seq 1 "${RUNS}"); do
  for pattern in "${PATTERNS[@]}"; do
    read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
    for transport in "${PATTERN_XPORTS[@]}"; do
      if ! transport_enabled "${transport}"; then
        continue
      fi
      if ! pattern_transport_supported "${pattern}" "${transport}"; then
        echo "UNSUPPORTED,current,${pattern},${transport}" >> "${RESULTS_FILE}"
        unsupported=$((unsupported + 1))
        continue
      fi

      transport_unsupported=0
      for size in "${SIZES[@]}"; do
        case_log="${TMP_DIR}/${pattern}_${transport}_${size}_run${run}.log"
        if go run ./perf/single \
          --pattern "${pattern}" \
          --transport "${transport}" \
          --msg-size "${size}" \
          --warmup "${WARMUP}" \
          --duration "${DURATION}" \
          > "${case_log}" 2>&1; then
          append_case_output "${case_log}"
          case_result_lines="$(count_result_lines "${pattern}" "${transport}" "${size}" "${case_log}")"
          if [[ "${case_result_lines}" -eq 0 ]]; then
            echo "FAIL,current,${pattern},${transport},${size},no_result_lines" >> "${RESULTS_FILE}"
            fail=$((fail + 1))
          else
            result_lines=$((result_lines + case_result_lines))
          fi
        else
          if is_unsupported_output "${case_log}"; then
            echo "UNSUPPORTED,current,${pattern},${transport}" >> "${RESULTS_FILE}"
            unsupported=$((unsupported + 1))
            transport_unsupported=1
            break
          fi
          append_case_output "${case_log}"
          echo "FAIL,current,${pattern},${transport},${size},exit_nonzero" >> "${RESULTS_FILE}"
          fail=$((fail + 1))
        fi
      done

      if [[ "${transport_unsupported}" -eq 1 ]]; then
        continue
      fi
    done
  done
done

{
  echo
  echo "## Effective Options (result)"
  echo "- lang: go"
  echo "- suite: single"
  echo "- pattern: ${PATTERN}"
  echo "- duration: ${DURATION}s"
  echo "- warmup: ${WARMUP}s"
  echo "- msg_sizes: ${MSG_SIZES}"
  echo "- transports: ${TRANSPORTS:-auto}"
  echo "- runs: ${RUNS}"
  echo "- result_lines: ${result_lines}"
  echo "- unsupported: ${unsupported}"
  echo "- fail: ${fail}"
  if [[ "${fail}" -eq 0 ]]; then
    echo "- status: complete"
  else
    echo "- status: partial"
  fi
} >> "${RESULTS_FILE}"

if [[ -n "${OUTPUT_FILE}" ]]; then
  cp "${RESULTS_FILE}" "${OUTPUT_FILE}"
fi

cat "${RESULTS_FILE}"
echo "Results saved to: ${RESULTS_FILE}"

if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi
