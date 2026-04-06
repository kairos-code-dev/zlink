#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${PROJECT_DIR}/../.." && pwd)"
STREAM_CLIENT="${REPO_DIR}/core/build/bin/perf_stream_client"

source "$HOME/.cargo/env" 2>/dev/null || true

PATTERN="ALL"
RECV_MODE="recv"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
WARMUP="${PERF_SINGLE_WARMUP_SECONDS:-2}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-tcp}"
RUNS="${PERF_RUNS:-1}"
CLIENTS="${PERF_MULTI_CLIENTS:-100}"
RESULTS_DIR="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results/multi/report}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"

print_help() {
    cat <<'EOF'
Usage: bindings/rust/perf/run_benchmarks_multi.sh [options]

Options:
  -h, --help
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

Notes:
  - MULTI_STREAM uses the shared core perf_stream_client required by policy.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_help
            exit 0
            ;;
        --pattern)     PATTERN="$2";     shift 2 ;;
        --recv)        RECV_MODE="$2";   shift 2 ;;
        --duration)    DURATION="$2";    shift 2 ;;
        --warmup)      WARMUP="$2";      shift 2 ;;
        --msg-sizes)   MSG_SIZES="$2";   shift 2 ;;
        --transports)  TRANSPORTS="$2";  shift 2 ;;
        --runs)        RUNS="$2";        shift 2 ;;
        --clients)     CLIENTS="$2";     shift 2 ;;
        --results-dir) RESULTS_DIR="$2"; shift 2 ;;
        --results-tag) RESULTS_TAG="$2"; shift 2 ;;
        *)             shift ;;
    esac
done

PLATFORM="linux"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""; [[ -n "${RESULTS_TAG}" ]] && TAG_SUFFIX="_${RESULTS_TAG}"
RESULTS_FILE="${RESULTS_DIR}/perf_${PLATFORM}_${RECV_MODE}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"

export PERF_MULTI_CLIENTS="${CLIENTS}"
export PERF_SINGLE_DURATION_SECONDS="${DURATION}"
export PERF_SINGLE_WARMUP_SECONDS="${WARMUP}"
export LD_LIBRARY_PATH="${PROJECT_DIR}/native/linux-x86_64:${LD_LIBRARY_PATH:-}"

echo "Building multi perf binaries..."
(cd "${SCRIPT_DIR}/multi" && cargo build --release --quiet 2>/dev/null || true)
BIN_DIR="${SCRIPT_DIR}/multi/target/release"

if [[ "${PATTERN}" == "ALL" ]]; then
    PATTERNS=("MULTI_DEALER_DEALER" "MULTI_DEALER_ROUTER" "MULTI_ROUTER_ROUTER" "MULTI_PUBSUB" "MULTI_SPOT" "MULTI_STREAM")
else
    IFS=',' read -ra PATTERNS <<< "${PATTERN}"
fi

IFS=',' read -ra SIZE_LIST <<< "${MSG_SIZES}"
IFS=',' read -ra TRANSPORT_LIST <<< "${TRANSPORTS}"

SERVER_READY_TIMEOUT=10

# Output both to stdout and results file
exec > >(tee -a "${RESULTS_FILE}") 2>&1

echo "## Effective Options (start)"
echo "  pattern:   ${PATTERN}"
echo "  recv:      ${RECV_MODE}"
echo "  duration:  ${DURATION}s"
echo "  warmup:    ${WARMUP}s"
echo "  msg_sizes: ${MSG_SIZES}"
echo "  transports: ${TRANSPORTS}"
echo "  clients:   ${CLIENTS}"
echo "  runs:      ${RUNS}"
echo "## Effective Options (end)"
echo ""

for run in $(seq 1 "${RUNS}"); do
    [[ "${RUNS}" -gt 1 ]] && echo "--- Run ${run}/${RUNS} ---"
    for pat in "${PATTERNS[@]}"; do
        SERVER_BIN=""
        CLIENT_BIN=""
        case "${pat}" in
            MULTI_DEALER_DEALER)
                SERVER_BIN="${BIN_DIR}/perf_multi_dealer_dealer_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_dealer_dealer_client" ;;
            MULTI_DEALER_ROUTER)
                SERVER_BIN="${BIN_DIR}/perf_multi_dealer_router_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_dealer_router_client" ;;
            MULTI_PUBSUB)
                SERVER_BIN="${BIN_DIR}/perf_multi_pubsub_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_pubsub_client" ;;
            MULTI_ROUTER_ROUTER)
                SERVER_BIN="${BIN_DIR}/perf_multi_router_router_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_router_router_client" ;;
            MULTI_SPOT)
                SERVER_BIN="${BIN_DIR}/perf_multi_spot_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_spot_client" ;;
            MULTI_STREAM)
                SERVER_BIN="${BIN_DIR}/perf_multi_stream_server"
                CLIENT_BIN="" ;;
            *)
                echo "UNSUPPORTED,rust,${pat},unknown,pattern not implemented"
                continue ;;
        esac

        for transport in "${TRANSPORT_LIST[@]}"; do
            for size in "${SIZE_LIST[@]}"; do
                echo ""
                echo ">> ${pat} transport=${transport} size=${size} clients=${CLIENTS}"

                SRV_OUT=$(mktemp)
                "${SERVER_BIN}" "${transport}" "${size}" > "${SRV_OUT}" 2>&1 &
                SERVER_PID=$!

                # Wait for READY
                ENDPOINT=""
                for _attempt in $(seq 1 ${SERVER_READY_TIMEOUT}); do
                    sleep 0.5
                    READY_LINE=$(head -1 "${SRV_OUT}" 2>/dev/null || true)
                    if [[ "${READY_LINE}" == READY,* ]]; then
                        ENDPOINT="${READY_LINE#READY,}"
                        ENDPOINT="${ENDPOINT//0.0.0.0/127.0.0.1}"
                        break
                    fi
                done

                if [[ -z "${ENDPOINT}" ]]; then
                    echo "FAIL,rust,${pat},${transport},${size},server_ready_timeout"
                    kill "${SERVER_PID}" 2>/dev/null || true
                    wait "${SERVER_PID}" 2>/dev/null || true
                    rm -f "${SRV_OUT}"
                    continue
                fi

                if [[ "${pat}" == "MULTI_STREAM" ]]; then
                    if [[ ! -x "${STREAM_CLIENT}" ]]; then
                        echo "FAIL,rust,${pat},${transport},${size},missing_shared_stream_client"
                    else
                        "${STREAM_CLIENT}" --transport "${transport}" --pattern STREAM \
                            --sizes "${size}" --runs 1 --warmup "${WARMUP}" \
                            --duration "${DURATION}" --ccu "${CLIENTS}" \
                            --print-perf-result 2 --send-stop-token 1 \
                            --endpoint "${ENDPOINT}" 2>&1 \
                            | sed 's/^RESULT,current,STREAM,/RESULT,current,MULTI_STREAM,/' || \
                            echo "FAIL,rust,${pat},${transport},${size},client_error"
                    fi
                else
                    "${CLIENT_BIN}" "${transport}" "${size}" "${ENDPOINT}" 2>&1 || \
                        echo "FAIL,rust,${pat},${transport},${size},client_error"
                fi

                kill "${SERVER_PID}" 2>/dev/null || true
                wait "${SERVER_PID}" 2>/dev/null || true
                grep "^RESULT" "${SRV_OUT}" 2>/dev/null || true
                rm -f "${SRV_OUT}"

                sleep 1
            done
            sleep 1
        done
        sleep 1
    done
done

echo ""
echo "Results saved to: ${RESULTS_FILE}"
