#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PR_GATE_SH="${SCRIPT_DIR}/run_stream_pr_gate.sh"
COMPARE_SH="${SCRIPT_DIR}/run_stream_compare.sh"

RESULT_ROOT="${RESULT_ROOT:-${SCRIPT_DIR}/result/nightly_gate_$(date +%Y%m%d_%H%M%S)}"
BASELINE_ROOT="${BASELINE_ROOT:-}"

DURATION=5
REPEATS=3
INFLIGHT=1
CLIENT_IO_THREADS=1
SERVER_IO_THREADS=1
ALL_CCU=1000
HIGH_CCU=10000
THROUGHPUT_MIN_RATIO="${THROUGHPUT_MIN_RATIO:-0.97}"
P99_MAX_RATIO="${P99_MAX_RATIO:-1.05}"

usage()
{
    cat <<'USAGE'
Usage:
  run_stream_nightly_gate.sh [options]

Options:
  --result-root <dir>           output root
  --baseline-root <dir>         forwarded to PR gate
  --duration <sec>              duration seconds (default: 5)
  --repeats <N>                 repeats (default: 3)
  --inflight <N>                inflight per conn (default: 1)
  --client-io-threads <N>       client io threads (default: 1)
  --server-io-threads <N>       server io threads (default: 1)
  --all-ccu <N>                 CCU for all/all run (default: 1000)
  --high-ccu <N>                CCU for zlink high-ccu runs (default: 10000)
  --throughput-min-ratio <F>    forwarded to PR gate (default: 0.97)
  --p99-max-ratio <F>           forwarded to PR gate (default: 1.05)
  -h, --help

Nightly flow:
  1) PR non-regression gate (zlink size 64/1024 baseline compare)
  2) stack all / size all run
  3) zlink high-ccu runs (size 64, 1024)
USAGE
}

validate_int()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value < 1 )); then
        echo "invalid ${name}: ${value}" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --result-root)
            RESULT_ROOT="${2:-}"
            shift 2
            ;;
        --baseline-root)
            BASELINE_ROOT="${2:-}"
            shift 2
            ;;
        --duration)
            DURATION="${2:-}"
            shift 2
            ;;
        --repeats)
            REPEATS="${2:-}"
            shift 2
            ;;
        --inflight)
            INFLIGHT="${2:-}"
            shift 2
            ;;
        --client-io-threads)
            CLIENT_IO_THREADS="${2:-}"
            shift 2
            ;;
        --server-io-threads)
            SERVER_IO_THREADS="${2:-}"
            shift 2
            ;;
        --all-ccu)
            ALL_CCU="${2:-}"
            shift 2
            ;;
        --high-ccu)
            HIGH_CCU="${2:-}"
            shift 2
            ;;
        --throughput-min-ratio)
            THROUGHPUT_MIN_RATIO="${2:-}"
            shift 2
            ;;
        --p99-max-ratio)
            P99_MAX_RATIO="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

validate_int "--duration" "${DURATION}"
validate_int "--repeats" "${REPEATS}"
validate_int "--inflight" "${INFLIGHT}"
validate_int "--client-io-threads" "${CLIENT_IO_THREADS}"
validate_int "--server-io-threads" "${SERVER_IO_THREADS}"
validate_int "--all-ccu" "${ALL_CCU}"
validate_int "--high-ccu" "${HIGH_CCU}"

mkdir -p "${RESULT_ROOT}"

echo "[NIGHTLY] step=1 pr-gate"
PR_GATE_ARGS=(
    --result-root "${RESULT_ROOT}/pr_gate"
    --ccu "${ALL_CCU}"
    --duration "${DURATION}"
    --repeats "${REPEATS}"
    --inflight "${INFLIGHT}"
    --client-io-threads "${CLIENT_IO_THREADS}"
    --server-io-threads "${SERVER_IO_THREADS}"
    --throughput-min-ratio "${THROUGHPUT_MIN_RATIO}"
    --p99-max-ratio "${P99_MAX_RATIO}"
)
if [[ -n "${BASELINE_ROOT}" ]]; then
    PR_GATE_ARGS+=(--baseline-root "${BASELINE_ROOT}")
fi
"${PR_GATE_SH}" "${PR_GATE_ARGS[@]}"

echo "[NIGHTLY] step=2 all-stacks/all-sizes"
RESULT_DIR="${RESULT_ROOT}/all_stacks_all_sizes" "${COMPARE_SH}" \
    --stack all \
    --size all \
    --ccu "${ALL_CCU}" \
    --duration "${DURATION}" \
    --repeats "${REPEATS}" \
    --inflight "${INFLIGHT}" \
    --client-io-threads "${CLIENT_IO_THREADS}" \
    --server-io-threads "${SERVER_IO_THREADS}"

echo "[NIGHTLY] step=3 zlink high-ccu size=64"
RESULT_DIR="${RESULT_ROOT}/zlink_highccu_s64" "${COMPARE_SH}" \
    --stack zlink \
    --size 64 \
    --ccu "${HIGH_CCU}" \
    --duration "${DURATION}" \
    --repeats "${REPEATS}" \
    --inflight "${INFLIGHT}" \
    --client-io-threads "${CLIENT_IO_THREADS}" \
    --server-io-threads "${SERVER_IO_THREADS}"

echo "[NIGHTLY] step=4 zlink high-ccu size=1024"
RESULT_DIR="${RESULT_ROOT}/zlink_highccu_s1024" "${COMPARE_SH}" \
    --stack zlink \
    --size 1024 \
    --ccu "${HIGH_CCU}" \
    --duration "${DURATION}" \
    --repeats "${REPEATS}" \
    --inflight "${INFLIGHT}" \
    --client-io-threads "${CLIENT_IO_THREADS}" \
    --server-io-threads "${SERVER_IO_THREADS}"

echo "[NIGHTLY] PASS result_root=${RESULT_ROOT}"
