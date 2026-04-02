#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

source "$HOME/.cargo/env" 2>/dev/null || true

# -- Defaults (matching core/perf) -------------------------------------------
PATTERN="ALL"
RECV_MODE="callback"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
WARMUP="${PERF_SINGLE_WARMUP_SECONDS:-2}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-inproc}"
RUNS="${PERF_RUNS:-1}"
RESULTS_DIR="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results/single/report}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"

# -- Parse CLI options -------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --pattern)   PATTERN="$2";   shift 2 ;;
        --recv)      RECV_MODE="$2"; shift 2 ;;
        --duration)  DURATION="$2";  shift 2 ;;
        --warmup)    WARMUP="$2";    shift 2 ;;
        --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
        --transports) TRANSPORTS="$2"; shift 2 ;;
        --runs)      RUNS="$2";      shift 2 ;;
        --results-dir) RESULTS_DIR="$2"; shift 2 ;;
        --results-tag) RESULTS_TAG="$2"; shift 2 ;;
        *)           shift ;;
    esac
done

# -- Platform ----------------------------------------------------------------
case "$(uname -s)" in
    Linux*)  PLATFORM="linux" ;;
    Darwin*) PLATFORM="macos" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)       PLATFORM="linux" ;;
esac

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""
if [[ -n "${RESULTS_TAG}" ]]; then
    TAG_SUFFIX="_${RESULTS_TAG}"
fi
RESULTS_FILE="${RESULTS_DIR}/perf_${PLATFORM}_${RECV_MODE}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"

# -- Build -------------------------------------------------------------------
echo "Building perf binaries..."
(cd "${SCRIPT_DIR}/single" && cargo build --release --quiet)

SINGLE_DIR="${SCRIPT_DIR}/single/target/release"

# Native library path
export LD_LIBRARY_PATH="${PROJECT_DIR}/native/linux-x86_64:${LD_LIBRARY_PATH:-}"

# -- Effective options -------------------------------------------------------
echo "## Effective Options (start)"
echo "  pattern:   ${PATTERN}"
echo "  recv:      ${RECV_MODE}"
echo "  duration:  ${DURATION}s"
echo "  warmup:    ${WARMUP}s"
echo "  msg_sizes: ${MSG_SIZES}"
echo "  transports: ${TRANSPORTS}"
echo "  runs:      ${RUNS}"
echo "  results:   ${RESULTS_FILE}"
echo "## Effective Options (end)"
echo ""

# -- Pattern list ------------------------------------------------------------
IFS=',' read -ra SIZE_LIST <<< "${MSG_SIZES}"
IFS=',' read -ra TRANSPORT_LIST <<< "${TRANSPORTS}"

if [[ "${PATTERN}" == "ALL" ]]; then
    PATTERNS=("PAIR" "PUBSUB" "DEALER_DEALER" "DEALER_ROUTER" "ROUTER_ROUTER" "SPOT")
else
    IFS=',' read -ra PATTERNS <<< "${PATTERN}"
fi

# -- Run benchmarks ----------------------------------------------------------
{
echo "## Effective Options (start)"
echo "  pattern:   ${PATTERN}"
echo "  recv:      ${RECV_MODE}"
echo "  duration:  ${DURATION}s"
echo "  warmup:    ${WARMUP}s"
echo "  msg_sizes: ${MSG_SIZES}"
echo "  transports: ${TRANSPORTS}"
echo ""

for run in $(seq 1 "${RUNS}"); do
    [[ "${RUNS}" -gt 1 ]] && echo "--- Run ${run}/${RUNS} ---"
    for pat in "${PATTERNS[@]}"; do
        BIN=""
        case "${pat}" in
            PAIR)            BIN="${SINGLE_DIR}/perf_pair" ;;
            PUBSUB)          BIN="${SINGLE_DIR}/perf_pubsub" ;;
            DEALER_DEALER)   BIN="${SINGLE_DIR}/perf_dealer_dealer" ;;
            DEALER_ROUTER)   BIN="${SINGLE_DIR}/perf_dealer_router" ;;
            ROUTER_ROUTER)   BIN="${SINGLE_DIR}/perf_router_router" ;;
            SPOT)            BIN="${SINGLE_DIR}/perf_spot" ;;
            *)               echo "UNSUPPORTED,rust,${pat},unknown,pattern not implemented"; continue ;;
        esac
        for transport in "${TRANSPORT_LIST[@]}"; do
            for size in "${SIZE_LIST[@]}"; do
                echo ""
                echo ">> ${pat} transport=${transport} size=${size}"
                "${BIN}" \
                    --pattern "${pat}" \
                    --transport "${transport}" \
                    --msg-size "${size}" \
                    --warmup "${WARMUP}" \
                    --duration "${DURATION}" \
                    2>&1 || echo "FAIL,rust,${pat},${transport},${size},error"
            done
        done
    done
done
} | tee "${RESULTS_FILE}"

echo ""
echo "Results saved to: ${RESULTS_FILE}"
