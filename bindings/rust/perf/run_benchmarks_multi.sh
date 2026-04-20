#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${PROJECT_DIR}/../.." && pwd)"
STREAM_BUILD_DIR="${REPO_DIR}/core/build"
STREAM_CLIENT="${STREAM_BUILD_DIR}/bin/perf_stream_client"
REUSE_BUILD=0
CLEAN_BUILD=0

source "$HOME/.cargo/env" 2>/dev/null || true

PATTERN="ALL"
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-tcp,tls,ws,wss}"
RUNS="1"
CLIENTS="${PERF_MULTI_CLIENTS:-100}"
RUN_COOLDOWN_MS="${PERF_MULTI_RUN_COOLDOWN_MS:-3000}"
RESULTS_DIR="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results/multi/report}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"
OUTPUT_FILE=""
PIN_CPU=0
BUILD_DIR=""
EXPLICIT_MSG_SIZES=0
COMMON_IO_THREADS="${PERF_IO_THREADS:-}"
SERVER_IO_THREADS=""
CLIENT_IO_THREADS=""
HWM=""
SEND_HWM=""
RECV_HWM=""
SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-200}"
RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-200}"
CONNECT_CONCURRENCY=""
TRANSPORT_TRANSITION_MS="${PERF_MULTI_TRANSPORT_TRANSITION_MS:-3000}"
PATTERN_TRANSITION_MS="${PERF_MULTI_PATTERN_TRANSITION_MS:-3000}"
SERVER_READY_TIMEOUT_MS="${PERF_MULTI_SERVER_READY_TIMEOUT_MS:-10000}"
SERVER_SHUTDOWN_TIMEOUT_MS="${PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS:-5000}"
CONNECT_READY_TIMEOUT_MS="${PERF_MULTI_CONNECT_READY_TIMEOUT_MS:-5000}"
MONITOR_HWM="${PERF_MULTI_MONITOR_HWM:-1000}"
SERVER_BIND_PORT="0"
ENV_PERF_IO_THREADS="${PERF_IO_THREADS:-}"
ENV_MULTI_SERVER_IO_THREADS="${PERF_MULTI_SERVER_IO_THREADS:-}"
ENV_MULTI_CLIENT_IO_THREADS="${PERF_MULTI_CLIENT_IO_THREADS:-}"
ENV_MULTI_STREAM_SERVER_IO_THREADS="${PERF_MULTI_STREAM_SERVER_IO_THREADS:-}"
ENV_MULTI_STREAM_CLIENT_IO_THREADS="${PERF_MULTI_STREAM_CLIENT_IO_THREADS:-}"
ENV_MULTI_HWM="${PERF_MULTI_HWM:-}"
ENV_MULTI_SNDHWM="${PERF_MULTI_SNDHWM:-}"
ENV_MULTI_RCVHWM="${PERF_MULTI_RCVHWM:-}"
[[ -n "${PERF_MSG_SIZES+x}" ]] && EXPLICIT_MSG_SIZES=1

print_help() {
    cat <<'EOF'
Usage: bindings/rust/perf/run_benchmarks_multi.sh [options]

Options:
  -h, --help
  --pattern NAME
  --duration N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --clients N
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
  --results-dir PATH
  --results-tag NAME
  --output PATH

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
        --duration)    DURATION="$2";    shift 2 ;;
        --msg-sizes)   MSG_SIZES="$2"; EXPLICIT_MSG_SIZES=1; shift 2 ;;
        --transports)  TRANSPORTS="$2";  shift 2 ;;
        --runs)        RUNS="$2";        shift 2 ;;
        --clients)     CLIENTS="$2";     shift 2 ;;
        --build-dir)   BUILD_DIR="$2";   shift 2 ;;
        --io-threads) COMMON_IO_THREADS="$2"; shift 2 ;;
        --server-io-threads) SERVER_IO_THREADS="$2"; shift 2 ;;
        --client-io-threads) CLIENT_IO_THREADS="$2"; shift 2 ;;
        --hwm) HWM="$2"; shift 2 ;;
        --send-hwm) SEND_HWM="$2"; shift 2 ;;
        --recv-hwm) RECV_HWM="$2"; shift 2 ;;
        --sndtimeo|--send-timeout-ms) SNDTIMEO_MS="$2"; shift 2 ;;
        --rcvtimeo|--recv-timeout-ms) RCVTIMEO_MS="$2"; shift 2 ;;
        --connect-concurrency) CONNECT_CONCURRENCY="$2"; shift 2 ;;
        --transport-transition-ms) TRANSPORT_TRANSITION_MS="$2"; shift 2 ;;
        --pattern-transition-ms) PATTERN_TRANSITION_MS="$2"; shift 2 ;;
        --server-ready-timeout-ms) SERVER_READY_TIMEOUT_MS="$2"; shift 2 ;;
        --connect-ready-timeout-ms) CONNECT_READY_TIMEOUT_MS="$2"; shift 2 ;;
        --monitor-hwm) MONITOR_HWM="$2"; shift 2 ;;
        --server-shutdown-timeout-ms) SERVER_SHUTDOWN_TIMEOUT_MS="$2"; shift 2 ;;
        --server-bind-port) SERVER_BIND_PORT="$2"; shift 2 ;;
        --results-dir) RESULTS_DIR="$2"; shift 2 ;;
        --results-tag) RESULTS_TAG="$2"; shift 2 ;;
        --output)      OUTPUT_FILE="$2"; shift 2 ;;
        --reuse-build) REUSE_BUILD=1; shift ;;
        --clean-build) CLEAN_BUILD=1; shift ;;
        --pin-cpu) PIN_CPU=1; shift ;;
        *)             echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ "${REUSE_BUILD}" -eq 1 && "${CLEAN_BUILD}" -eq 1 ]]; then
    echo "--reuse-build and --clean-build are mutually exclusive" >&2
    exit 1
fi

case "$(uname -s)" in
    Linux*)  PLATFORM="linux" ;;
    Darwin*) PLATFORM="macos" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)       PLATFORM="linux" ;;
esac
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""; [[ -n "${RESULTS_TAG}" ]] && TAG_SUFFIX="_${RESULTS_TAG}"
RESULTS_FILE="${RESULTS_DIR}/perf_rust_multi_${PLATFORM}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"

prune_reports() {
    local report_dir="$1"
    local max_files="${PERF_RESULTS_MAX_FILES:-100}"
    if [[ ! "${max_files}" =~ ^[0-9]+$ ]] || [[ "${max_files}" -lt 1 ]]; then
        echo "PERF_RESULTS_MAX_FILES must be >= 1" >&2
        exit 1
    fi

    local count
    count="$(find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' | wc -l | tr -d ' ')"
    if [[ -z "${count}" || "${count}" -le "${max_files}" ]]; then
        return
    fi
    find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' -printf '%f\n' \
        | sort \
        | head -n "$((count - max_files))" \
        | while read -r old_file; do
            rm -f "${report_dir}/${old_file}"
        done
}

normalize_patterns() {
    local raw="${1:-ALL}"
    python3 - "${raw}" <<'PY'
import sys

raw = sys.argv[1].upper()
allowed = {
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "SPOT",
    "SPOT_REQREP",
    "STREAM",
}

if raw == "ALL":
    print("MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM")
    raise SystemExit(0)

items = []
for token in raw.split(","):
    value = token.strip()
    if not value:
        continue
    if value.startswith("MULTI_"):
        value = value[len("MULTI_"):]
    if value not in allowed:
        raise SystemExit(f"unsupported multi pattern: {value}")
    items.append(f"MULTI_{value}")

if not items:
    raise SystemExit("no valid multi pattern specified")

print(",".join(items))
PY
}

default_msg_sizes_for_pattern() {
    local pattern="$1"
    if [[ "${EXPLICIT_MSG_SIZES}" -eq 1 ]]; then
        printf '%s' "${MSG_SIZES}"
        return
    fi
    case "${pattern}" in
        MULTI_STREAM)
            printf '%s' "${PERF_MULTI_STREAM_MSG_SIZES:-64,256,1024,65536}"
            ;;
        *)
            printf '%s' "${MSG_SIZES}"
            ;;
    esac
}

default_io_threads_for_pattern() {
    local pattern="$1"
    case "${pattern}" in
        MULTI_STREAM) printf '%s' "4" ;;
        *) printf '%s' "2" ;;
    esac
}

default_hwm_for_pattern() {
    printf '%s' "1000"
}

ensure_core_stream_client() {
    if [[ "${REUSE_BUILD}" -eq 1 ]]; then
        if [[ ! -x "${STREAM_CLIENT}" ]]; then
            echo "shared stream client not found for --reuse-build: ${STREAM_CLIENT}" >&2
            exit 1
        fi
        return
    fi

    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        rm -rf "${STREAM_BUILD_DIR}"
    fi
    cmake -S "${REPO_DIR}" -B "${STREAM_BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LTO=OFF \
        -DBUILD_BENCHMARKS=ON \
        -DZLINK_BUILD_TESTS=OFF \
        -DZLINK_BUILD_BENCH_ZMQ=OFF \
        -DZLINK_BUILD_BENCH_ZLINK=ON \
        -DZLINK_BUILD_BENCH_BEAST=OFF \
        -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF \
        -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF \
        -DZLINK_CXX_STANDARD=17 >/dev/null
    cmake --build "${STREAM_BUILD_DIR}" --target perf_stream_client >/dev/null
}

if [[ -n "${BUILD_DIR}" ]]; then
    TARGET_DIR="${BUILD_DIR}/rust-multi"
    STREAM_BUILD_DIR="${BUILD_DIR}/core"
    STREAM_CLIENT="${STREAM_BUILD_DIR}/bin/perf_stream_client"
else
    TARGET_DIR="${SCRIPT_DIR}/multi/target"
fi
BIN_DIR="${TARGET_DIR}/release"

export PERF_MULTI_DURATION_SECONDS="${DURATION}"
export LD_LIBRARY_PATH="${PROJECT_DIR}/native/linux-x86_64:${LD_LIBRARY_PATH:-}"
export PERF_MULTI_SERVER_BIND_PORT="${SERVER_BIND_PORT}"
export PERF_MULTI_MONITOR_HWM="${MONITOR_HWM}"
[[ -n "${CONNECT_CONCURRENCY}" ]] && export PERF_MULTI_CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY}"
if [[ -n "${COMMON_IO_THREADS}" ]]; then
    export PERF_IO_THREADS="${COMMON_IO_THREADS}"
elif [[ -n "${ENV_PERF_IO_THREADS}" ]]; then
    export PERF_IO_THREADS="${ENV_PERF_IO_THREADS}"
else
    unset PERF_IO_THREADS || true
fi
export PERF_MULTI_SNDTIMEO_MS="${SNDTIMEO_MS}"
export PERF_MULTI_RCVTIMEO_MS="${RCVTIMEO_MS}"
export PERF_MULTI_CONNECT_READY_TIMEOUT_MS="${CONNECT_READY_TIMEOUT_MS}"
export PERF_MULTI_SERVER_BIND_PORT="${SERVER_BIND_PORT}"

RUN_PREFIX=()
if [[ "${PIN_CPU}" -eq 1 ]]; then
    if [[ "$(uname -s)" != "Linux" ]]; then
        echo "--pin-cpu is only supported on Linux in this runner" >&2
        exit 1
    fi
    if ! command -v taskset >/dev/null 2>&1; then
        echo "--pin-cpu requires taskset" >&2
        exit 1
    fi
    RUN_PREFIX=("taskset" "-c" "0")
fi

if [[ "${REUSE_BUILD}" -eq 0 ]]; then
    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        (cd "${SCRIPT_DIR}/multi" && CARGO_TARGET_DIR="${TARGET_DIR}" cargo clean --quiet)
    fi
    (cd "${SCRIPT_DIR}/multi" && CARGO_TARGET_DIR="${TARGET_DIR}" cargo build --release --quiet)
elif [[ ! -x "${BIN_DIR}/perf_multi_dealer_dealer_server" ]]; then
    echo "existing multi perf binaries not found for --reuse-build: ${BIN_DIR}" >&2
    exit 1
fi

PATTERN="$(normalize_patterns "${PATTERN}")"
IFS=',' read -ra PATTERNS <<< "${PATTERN}"
if printf '%s\n' "${PATTERNS[@]}" | grep -qx 'MULTI_STREAM'; then
    ensure_core_stream_client
fi

IFS=',' read -ra TRANSPORT_LIST <<< "${TRANSPORTS}"
ONE_WAY_CLIENT_READY_TIMEOUT=10
SERVER_READY_TIMEOUT_SECONDS=$(( (SERVER_READY_TIMEOUT_MS + 999) / 1000 ))
SERVER_SHUTDOWN_TIMEOUT_SECONDS=$(( (SERVER_SHUTDOWN_TIMEOUT_MS + 999) / 1000 ))

TMP_METRICS="$(mktemp)"
TMP_CASES="$(mktemp)"
trap 'rm -f "${TMP_METRICS}" "${TMP_CASES}"' EXIT
METRICS_REGEX='^(throughput|bandwidth|latency|latency_p95|latency_p99)$'

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

shutdown_server() {
    local pid="$1"
    local control_fd="$2"

    if kill -0 "${pid}" 2>/dev/null; then
        printf 'STOP\n' >&"${control_fd}" || true
        exec {control_fd}>&- || true
        if wait_for_pid "${pid}" "${SERVER_SHUTDOWN_TIMEOUT_SECONDS}"; then
            return
        fi
        kill "${pid}" 2>/dev/null || true
        if wait_for_pid "${pid}" 2; then
            return
        fi
        kill -9 "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
}

wait_for_file_line() {
    local file_path="$1"
    local timeout_seconds="$2"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if [[ -s "${file_path}" ]]; then
            head -1 "${file_path}" 2>/dev/null || true
            return 0
        fi
        sleep 0.1
    done
    return 1
}

wait_for_file_prefix() {
    local file_path="$1"
    local prefix="$2"
    local timeout_seconds="$3"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if [[ -f "${file_path}" ]]; then
            local line
            line="$(awk -v prefix="${prefix}" 'index($0, prefix) == 1 { print; exit }' "${file_path}" 2>/dev/null || true)"
            if [[ -n "${line}" ]]; then
                printf '%s\n' "${line}"
                return 0
            fi
        fi
        sleep 0.1
    done
    return 1
}

read_first_case_signal() {
    local file_path="$1"
    awk '/^(READY,|UNSUPPORTED,)/ { print; exit }' "${file_path}" 2>/dev/null || true
}

wait_for_ready_or_unsupported() {
    local file_path="$1"
    local timeout_seconds="$2"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if [[ -f "${file_path}" ]]; then
            local line
            line="$(read_first_case_signal "${file_path}")"
            if [[ -n "${line}" ]]; then
                printf '%s\n' "${line}"
                return 0
            fi
        fi
        sleep 0.1
    done
    return 1
}

resolve_client_timeout_seconds() {
    local pattern="$1"
    local transport="$2"
    local size="$3"
    local duration="$4"
    local override="${PERF_MULTI_TIMEOUT_SECONDS:-}"
    local timeout_seconds=0

    if [[ -n "${override}" ]]; then
        printf '%s\n' "${override}"
        return
    fi

    if [[ "${pattern}" == "MULTI_STREAM" ]]; then
        timeout_seconds=$(( duration * 3 + 20 ))
        if (( timeout_seconds < 45 )); then
            timeout_seconds=45
        fi
        printf '%s\n' "${timeout_seconds}"
        return
    fi

    if [[ "${pattern}" == "MULTI_SPOT" || "${pattern}" == "MULTI_SPOT_REQREP" ]] \
        || { [[ "${transport}" == "tls" || "${transport}" == "wss" ]] && (( size >= 131072 )); }; then
        timeout_seconds=$(( duration * 6 + 30 ))
        if (( timeout_seconds < 90 )); then
            timeout_seconds=90
        fi
        printf '%s\n' "${timeout_seconds}"
        return
    fi

    timeout_seconds=$(( duration * 3 + 20 ))
    if (( timeout_seconds < 45 )); then
        timeout_seconds=45
    fi
    printf '%s\n' "${timeout_seconds}"
}

for run in $(seq 1 "${RUNS}"); do
    [[ "${RUNS}" -gt 1 ]] && echo "--- Run ${run}/${RUNS} ---"
    for pat_index in "${!PATTERNS[@]}"; do
        pat="${PATTERNS[pat_index]}"
        IFS=',' read -ra SIZE_LIST <<< "$(default_msg_sizes_for_pattern "${pat}")"
        PATTERN_CLIENTS="${CLIENTS}"
        if [[ "${pat}" == "MULTI_STREAM" && "${CLIENTS}" == "100" ]]; then
            PATTERN_CLIENTS="10000"
        fi
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
            MULTI_SPOT_REQREP)
                SERVER_BIN="${BIN_DIR}/perf_multi_spot_reqrep_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_spot_reqrep_client" ;;
            MULTI_STREAM)
                SERVER_BIN="${BIN_DIR}/perf_multi_stream_server"
                CLIENT_BIN="" ;;
            *)
                echo "UNSUPPORTED,rust,${pat},unknown"
                continue ;;
        esac

        for transport_index in "${!TRANSPORT_LIST[@]}"; do
            transport="${TRANSPORT_LIST[transport_index]}"
            for size in "${SIZE_LIST[@]}"; do
                CLIENT_TIMEOUT_SECONDS="$(resolve_client_timeout_seconds "${pat}" "${transport}" "${size}" "${DURATION}")"
                export PERF_MULTI_CLIENTS="${PATTERN_CLIENTS}"
                pattern_default_io_threads="$(default_io_threads_for_pattern "${pat}")"
                pattern_default_hwm="$(default_hwm_for_pattern "${pat}")"
                if [[ "${pat}" == "MULTI_STREAM" ]]; then
                    pattern_server_io_threads="${ENV_MULTI_STREAM_SERVER_IO_THREADS:-${ENV_MULTI_SERVER_IO_THREADS:-${pattern_default_io_threads}}}"
                    pattern_client_io_threads="${ENV_MULTI_STREAM_CLIENT_IO_THREADS:-${ENV_MULTI_CLIENT_IO_THREADS:-${pattern_default_io_threads}}}"
                else
                    pattern_server_io_threads="${ENV_MULTI_SERVER_IO_THREADS:-${pattern_default_io_threads}}"
                    pattern_client_io_threads="${ENV_MULTI_CLIENT_IO_THREADS:-${pattern_default_io_threads}}"
                fi
                export PERF_MULTI_SERVER_IO_THREADS="${SERVER_IO_THREADS:-${COMMON_IO_THREADS:-${ENV_PERF_IO_THREADS:-${pattern_server_io_threads}}}}"
                export PERF_MULTI_CLIENT_IO_THREADS="${CLIENT_IO_THREADS:-${COMMON_IO_THREADS:-${ENV_PERF_IO_THREADS:-${pattern_client_io_threads}}}}"
                export PERF_MULTI_HWM="${HWM:-${ENV_MULTI_HWM:-${pattern_default_hwm}}}"
                export PERF_MULTI_SNDHWM="${SEND_HWM:-${ENV_MULTI_SNDHWM:-${PERF_MULTI_HWM}}}"
                export PERF_MULTI_RCVHWM="${RECV_HWM:-${ENV_MULTI_RCVHWM:-${PERF_MULTI_HWM}}}"
                case_status="success"
                case_reason=""
                CLIENT_OUTPUT=""
                SRV_OUT=$(mktemp)
                SERVER_FIFO="$(mktemp -u)"
                mkfifo "${SERVER_FIFO}"
                "${RUN_PREFIX[@]}" "${SERVER_BIN}" "${transport}" "${size}" < "${SERVER_FIFO}" > "${SRV_OUT}" 2>&1 &
                SERVER_PID=$!
                exec {SERVER_CONTROL_FD}> "${SERVER_FIFO}"
                rm -f "${SERVER_FIFO}"

                # Wait for READY
                ENDPOINT=""
                READY_LINE="$(wait_for_ready_or_unsupported "${SRV_OUT}" "${SERVER_READY_TIMEOUT_SECONDS}" || true)"
                if [[ "${READY_LINE}" == UNSUPPORTED,* ]]; then
                    case_status="unsupported"
                    case_reason="${READY_LINE//,/;}"
                    shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                    rm -f "${SRV_OUT}"
                    printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
                    continue
                fi
                if [[ "${READY_LINE}" == READY,* ]]; then
                    ENDPOINT="${READY_LINE#READY,}"
                    ENDPOINT="${ENDPOINT//0.0.0.0/127.0.0.1}"
                fi

                if [[ -z "${ENDPOINT}" ]]; then
                    case_status="fail"
                    case_reason="server_ready_timeout"
                    shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                    rm -f "${SRV_OUT}"
                    printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
                    continue
                fi

                CONTROL_ENDPOINT=""
                if [[ "${pat}" == "MULTI_SPOT" || "${pat}" == "MULTI_SPOT_REQREP" ]]; then
                    CONTROL_LINE="$(wait_for_file_prefix "${SRV_OUT}" "CONTROL_READY," "${SERVER_READY_TIMEOUT_SECONDS}" || true)"
                    if [[ "${CONTROL_LINE}" == CONTROL_READY,* ]]; then
                        CONTROL_ENDPOINT="${CONTROL_LINE#CONTROL_READY,}"
                        CONTROL_ENDPOINT="${CONTROL_ENDPOINT//0.0.0.0/127.0.0.1}"
                    fi
                    if [[ -z "${CONTROL_ENDPOINT}" ]]; then
                        case_status="fail"
                        case_reason="control_ready_timeout"
                        shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                        rm -f "${SRV_OUT}"
                        printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
                        continue
                    fi
                fi

                ROUTE_INFO=""
                if [[ "${pat}" == "MULTI_SPOT_REQREP" ]]; then
                    ROUTE_LINE="$(wait_for_file_prefix "${SRV_OUT}" "ROUTE_READY," "${SERVER_READY_TIMEOUT_SECONDS}" || true)"
                    if [[ "${ROUTE_LINE}" == ROUTE_READY,* ]]; then
                        ROUTE_INFO="${ROUTE_LINE#ROUTE_READY,}"
                    fi
                    if [[ -z "${ROUTE_INFO}" ]]; then
                        case_status="fail"
                        case_reason="route_ready_timeout"
                        shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                        rm -f "${SRV_OUT}"
                        printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
                        continue
                    fi
                fi

                if [[ "${pat}" == "MULTI_DEALER_DEALER" || "${pat}" == "MULTI_PUBSUB" ]]; then
                    CLIENT_OUT="$(mktemp)"
                    CLIENT_ERR="$(mktemp)"
                    CLIENT_FIFO="$(mktemp -u)"
                    mkfifo "${CLIENT_FIFO}"
                    "${RUN_PREFIX[@]}" "${CLIENT_BIN}" "${transport}" "${size}" "${ENDPOINT}" < "${CLIENT_FIFO}" > "${CLIENT_OUT}" 2> "${CLIENT_ERR}" &
                    CLIENT_PID=$!
                    exec {CLIENT_CONTROL_FD}> "${CLIENT_FIFO}"
                    rm -f "${CLIENT_FIFO}"

                    CLIENT_READY_LINE=""
                    if CLIENT_READY_LINE="$(wait_for_file_line "${CLIENT_OUT}" "${ONE_WAY_CLIENT_READY_TIMEOUT}")"; then
                        CLIENT_READY_LINE="${CLIENT_READY_LINE%%$'\n'*}"
                    fi
                    if [[ "${CLIENT_READY_LINE}" != "CLIENT_READY,${size}" ]]; then
                        case_status="fail"
                        case_reason="client_ready_timeout_or_invalid"
                    else
                        printf 'START,%s\n' "${size}" >&"${SERVER_CONTROL_FD}" || true
                        printf 'START,%s\n' "${size}" >&"${CLIENT_CONTROL_FD}" || true
                        printf 'PHASE_ACTIVE,%s\n' "${size}" >&"${CLIENT_CONTROL_FD}" || true
                    fi

                    if [[ "${case_status}" == "success" ]]; then
                        if ! wait_for_pid "${CLIENT_PID}" "${CLIENT_TIMEOUT_SECONDS}"; then
                            case_status="fail"
                            case_reason="binary_exit_or_timeout"
                            kill "${CLIENT_PID}" 2>/dev/null || true
                            wait "${CLIENT_PID}" 2>/dev/null || true
                        elif ! wait "${CLIENT_PID}"; then
                            case_status="fail"
                            case_reason="binary_exit_or_timeout"
                        fi
                    else
                        kill "${CLIENT_PID}" 2>/dev/null || true
                        wait "${CLIENT_PID}" 2>/dev/null || true
                    fi

                    exec {CLIENT_CONTROL_FD}>&- || true
                    OUTPUT=""
                    if [[ -f "${CLIENT_OUT}" ]]; then
                        CLIENT_OUTPUT="$(cat "${CLIENT_OUT}")"
                    fi
                    if [[ -s "${CLIENT_ERR}" ]]; then
                        if [[ -n "${CLIENT_OUTPUT}" ]]; then
                            CLIENT_OUTPUT+=$'\n'
                        fi
                        CLIENT_OUTPUT+="$(cat "${CLIENT_ERR}")"
                    fi
                    rm -f "${CLIENT_OUT}" "${CLIENT_ERR}"
                elif [[ "${pat}" == "MULTI_SPOT" || "${pat}" == "MULTI_SPOT_REQREP" ]]; then
                    CLIENT_OUT="$(mktemp)"
                    CLIENT_ERR="$(mktemp)"
                    CLIENT_FIFO="$(mktemp -u)"
                    mkfifo "${CLIENT_FIFO}"
                    CLIENT_ENDPOINT="${ENDPOINT},${CONTROL_ENDPOINT}"
                    if [[ "${pat}" == "MULTI_SPOT_REQREP" ]]; then
                        CLIENT_ENDPOINT="${CLIENT_ENDPOINT},${ROUTE_INFO}"
                    fi
                    "${RUN_PREFIX[@]}" "${CLIENT_BIN}" "${transport}" "${size}" "${CLIENT_ENDPOINT}" < "${CLIENT_FIFO}" > "${CLIENT_OUT}" 2> "${CLIENT_ERR}" &
                    CLIENT_PID=$!
                    exec {CLIENT_CONTROL_FD}> "${CLIENT_FIFO}"
                    rm -f "${CLIENT_FIFO}"

                    CLIENT_CONTROL_LINE="$(wait_for_file_prefix "${CLIENT_OUT}" "CLIENT_CONTROL_ENDPOINT," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
                    if [[ "${CLIENT_CONTROL_LINE}" != CLIENT_CONTROL_ENDPOINT,* ]]; then
                        case_status="fail"
                        case_reason="client_control_endpoint_timeout"
                    else
                        CLIENT_CONTROL_ENDPOINT="${CLIENT_CONTROL_LINE#CLIENT_CONTROL_ENDPOINT,}"
                        printf 'CONNECT_CONTROL,%s\n' "${CLIENT_CONTROL_ENDPOINT}" >&"${SERVER_CONTROL_FD}" || true
                        SERVER_CONTROL_CONNECTED="$(wait_for_file_prefix "${SRV_OUT}" "CONTROL_CONNECTED," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
                        if [[ "${SERVER_CONTROL_CONNECTED}" != "CONTROL_CONNECTED,${CLIENT_CONTROL_ENDPOINT}" ]]; then
                            case_status="fail"
                            case_reason="control_connect_timeout"
                        fi
                    fi

                    if [[ "${case_status}" == "success" ]]; then
                        CLIENT_READY_LINE="$(wait_for_file_prefix "${CLIENT_OUT}" "CLIENT_READY," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
                        if [[ "${CLIENT_READY_LINE}" != "CLIENT_READY,${size}" ]]; then
                            case_status="fail"
                            case_reason="client_ready_timeout_or_invalid"
                        else
                            printf 'START,%s\n' "${size}" >&"${SERVER_CONTROL_FD}" || true
                            printf 'START,%s\n' "${size}" >&"${CLIENT_CONTROL_FD}" || true
                        fi
                    fi

                    if [[ "${case_status}" == "success" ]]; then
                        if ! wait_for_pid "${CLIENT_PID}" "${CLIENT_TIMEOUT_SECONDS}"; then
                            case_status="fail"
                            case_reason="binary_exit_or_timeout"
                            kill "${CLIENT_PID}" 2>/dev/null || true
                            wait "${CLIENT_PID}" 2>/dev/null || true
                        elif ! wait "${CLIENT_PID}"; then
                            case_status="fail"
                            case_reason="binary_exit_or_timeout"
                        fi
                    else
                        kill "${CLIENT_PID}" 2>/dev/null || true
                        wait "${CLIENT_PID}" 2>/dev/null || true
                    fi

                    exec {CLIENT_CONTROL_FD}>&- || true
                    if [[ -f "${CLIENT_OUT}" ]]; then
                        CLIENT_OUTPUT="$(cat "${CLIENT_OUT}")"
                    fi
                    if [[ -s "${CLIENT_ERR}" ]]; then
                        if [[ -n "${CLIENT_OUTPUT}" ]]; then
                            CLIENT_OUTPUT+=$'\n'
                        fi
                        CLIENT_OUTPUT+="$(cat "${CLIENT_ERR}")"
                    fi
                    rm -f "${CLIENT_OUT}" "${CLIENT_ERR}"
                elif [[ "${pat}" == "MULTI_STREAM" ]]; then
                    if ! CLIENT_OUTPUT="$(timeout "${CLIENT_TIMEOUT_SECONDS}s" "${RUN_PREFIX[@]}" "${STREAM_CLIENT}" \
                        --transport "${transport}" \
                        --pattern STREAM \
                        --sizes "${size}" \
                        --runs 1 \
                        --duration "${DURATION}" \
                        --ccu "${PATTERN_CLIENTS}" \
                        --io-threads 4 \
                        --print-perf-result 1 \
                        --send-stop-token 0 \
                        --endpoint "${ENDPOINT}" 2>&1)"; then
                        case_status="fail"
                        case_reason="binary_exit_or_timeout"
                    fi
                else
                    if ! CLIENT_OUTPUT="$(timeout "${CLIENT_TIMEOUT_SECONDS}s" "${RUN_PREFIX[@]}" "${CLIENT_BIN}" "${transport}" "${size}" "${ENDPOINT}" 2>&1)"; then
                        case_status="fail"
                        case_reason="binary_exit_or_timeout"
                    fi
                fi

                shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                SERVER_OUTPUT=""
                if [[ -f "${SRV_OUT}" ]]; then
                    SERVER_OUTPUT="$(cat "${SRV_OUT}")"
                fi
                rm -f "${SRV_OUT}"
                OUTPUT="${SERVER_OUTPUT}"
                if [[ -n "${CLIENT_OUTPUT}" ]]; then
                    if [[ -n "${OUTPUT}" ]]; then
                        OUTPUT+=$'\n'
                    fi
                    OUTPUT+="${CLIENT_OUTPUT}"
                fi
                if [[ "${case_status}" == "success" ]]; then
                    unsupported_line="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^UNSUPPORTED,/ {print; exit}')"
                    if [[ -n "${unsupported_line}" ]]; then
                        case_status="unsupported"
                        case_reason="${unsupported_line}"
                    elif printf '%s\n' "${OUTPUT}" | grep -qi 'protocol not supported'; then
                        case_status="unsupported"
                        case_reason="protocol_not_supported"
                    fi
                fi
                if [[ "${case_status}" == "success" ]]; then
                    skip_line="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^SKIP,/ {print; exit}')"
                    if [[ -n "${skip_line}" ]]; then
                        case_status="skip"
                        case_reason="${skip_line}"
                    fi
                fi
                case_reason="${case_reason//,/;}"
                while IFS= read -r line; do
                    [[ "${line}" == RESULT,* ]] || continue
                    IFS=',' read -r tag lib result_pattern result_transport result_size metric value <<< "${line}"
                    [[ "${metric}" =~ ${METRICS_REGEX} ]] || continue
                    printf '%s,%s,%s,%s,%s,%s\n' \
                        "${pat}" "${transport}" "${size}" "${run}" "${metric}" "${value}" >> "${TMP_METRICS}"
                done <<< "${OUTPUT}"
                REQUIRED_COUNT="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^RESULT,/ && ($6=="throughput" || $6=="bandwidth" || $6=="latency" || $6=="latency_p95" || $6=="latency_p99") {count++} END {print count+0}')"
                if [[ "${case_status}" == "success" && "${REQUIRED_COUNT}" -ne 5 ]]; then
                    case_status="fail"
                    case_reason="missing_required_result_lines run=${run}"
                fi
                printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
            done
        if (( transport_index + 1 < ${#TRANSPORT_LIST[@]} )); then
            sleep "$(awk "BEGIN { printf \"%.3f\", ${TRANSPORT_TRANSITION_MS} / 1000 }")"
        fi
    done
    if (( pat_index + 1 < ${#PATTERNS[@]} )); then
        sleep "$(awk "BEGIN { printf \"%.3f\", ${PATTERN_TRANSITION_MS} / 1000 }")"
    fi
done
    if (( run < RUNS )); then
        sleep "$(awk "BEGIN { printf \"%.3f\", ${RUN_COOLDOWN_MS} / 1000 }")"
    fi
done
python3 - "${TMP_METRICS}" "${TMP_CASES}" "${RESULTS_FILE}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${CLIENTS}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${OUTPUT_FILE}" "${PIN_CPU}" \
  "${COMMON_IO_THREADS}" "${SERVER_IO_THREADS}" "${CLIENT_IO_THREADS}" "${HWM}" "${SEND_HWM}" \
  "${RECV_HWM}" "${SNDTIMEO_MS}" "${RCVTIMEO_MS}" "${RUN_COOLDOWN_MS}" \
  "${TRANSPORT_TRANSITION_MS}" "${PATTERN_TRANSITION_MS}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, cases_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, clients, runs, duration, results_tag, output_path, pin_cpu, common_io_threads, server_io_threads, client_io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, run_cooldown_ms, transport_transition_ms, pattern_transition_ms = sys.argv[1:]
runs = int(runs)
required_metrics = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
rows = defaultdict(lambda: defaultdict(list))
cases = {}
patterns = []
pattern_transports = defaultdict(list)
pattern_sizes = defaultdict(list)

with open(metrics_path, newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    for pattern, transport, size, run, metric, value in reader:
        key = (pattern, transport, int(size))
        if pattern not in patterns:
            patterns.append(pattern)
        if transport not in pattern_transports[pattern]:
            pattern_transports[pattern].append(transport)
        if int(size) not in pattern_sizes[pattern]:
            pattern_sizes[pattern].append(int(size))
        try:
            rows[key][metric].append(float(value))
        except ValueError:
            rows[key][metric].append(math.nan)

with open(cases_path, newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    for pattern, transport, size, status, reason in reader:
        size = int(size)
        cases[(pattern, transport, size)] = (status, reason)
        if pattern not in patterns:
            patterns.append(pattern)
        if transport not in pattern_transports[pattern]:
            pattern_transports[pattern].append(transport)
        if size not in pattern_sizes[pattern]:
            pattern_sizes[pattern].append(size)

for pattern in pattern_sizes:
    pattern_sizes[pattern].sort()

def median(values):
    usable = [v for v in values if not math.isnan(v)]
    if not usable:
        return math.nan
    usable.sort()
    mid = len(usable) // 2
    if len(usable) % 2:
        return usable[mid]
    return (usable[mid - 1] + usable[mid]) / 2.0

def fmt_rate(value):
    return "N/A" if math.isnan(value) else f"{value / 1000.0:.2f}"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} MB/s"

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} ms"

def fmt_metric(value):
    return "N/A" if math.isnan(value) else f"{value:.2f}"

expected = 0
actual = 0
lines = []
result_lines = []
failures = []

def emit(line=""):
    lines.append(line)

def emit_effective_options(section):
    emit(f"## Effective Options ({section})")
    emit("- lang: rust")
    emit("- suite: multi")
    emit(f"- runs: {runs}")
    emit(f"- patterns: {pattern_csv}")
    emit(f"- transports: {transports_csv}")
    emit(f"- msg_sizes: {msg_sizes_csv}")
    emit(f"- clients: {clients}")
    emit(f"- pin_cpu: {'on' if pin_cpu == '1' else 'off'}")
    emit(f"- io_threads: {common_io_threads or 'default(binding)'}")
    emit(f"- server_io_threads: {server_io_threads or 'default(binding)'}")
    emit(f"- client_io_threads: {client_io_threads or 'default(binding)'}")
    emit(f"- hwm: {hwm or 'default(binding)'}")
    emit(f"- send_hwm: {send_hwm or 'default(binding)'}")
    emit(f"- recv_hwm: {recv_hwm or 'default(binding)'}")
    emit(f"- send_timeout_ms: {sndtimeo_ms}")
    emit(f"- recv_timeout_ms: {rcvtimeo_ms}")
    emit(f"- duration_seconds: {duration}")
    emit(f"- run_cooldown_ms: {run_cooldown_ms}")
    emit(f"- transport_transition_ms: {transport_transition_ms}")
    emit(f"- pattern_transition_ms: {pattern_transition_ms}")
    if results_tag:
        emit(f"- results_tag: {results_tag}")
    emit("")

emit_effective_options("start")
emit("===============================================================================")
emit("")

def pattern_direction(pattern):
    return "echo" if pattern in {
        "MULTI_DEALER_ROUTER",
        "MULTI_ROUTER_ROUTER",
        "MULTI_STREAM",
        "MULTI_SPOT_REQREP",
    } else "one-way"

def rate_unit(pattern):
    return "Kops/s" if pattern_direction(pattern) == "echo" else "Kmsg/s"

def emit_table_header():
    emit("| Size | Throughput | Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) |")
    emit("|------|------------|-----------|--------------|-------------|-------------|")

def metric_value_for_run(key, metric, run_index):
    values = rows[key].get(metric, [])
    if run_index < len(values):
        return values[run_index]
    return math.nan

def emit_case_row(pattern, size, metric_values):
    emit(
        f"| {size}B | {fmt_rate(metric_values['throughput'])} {rate_unit(pattern)} | "
        f"{fmt_bandwidth(metric_values['bandwidth'])} | "
        f"{fmt_latency_ms(metric_values['latency'])} | "
        f"{fmt_latency_ms(metric_values['latency_p95'])} | "
        f"{fmt_latency_ms(metric_values['latency_p99'])} |"
    )

for pattern_index, pattern in enumerate(patterns):
    if pattern_index:
        emit("===============================================================================")
        emit("")
    emit(f"## PATTERN: {pattern} ({pattern_direction(pattern)})")
    emit("")
    for transport in pattern_transports[pattern]:
        emit(f"### Transport: {transport}")
        if runs > 1:
            for run_index in range(runs):
                emit(f"run {run_index + 1}/{runs}:")
                emit_table_header()
                for size in pattern_sizes[pattern]:
                    key = (pattern, transport, size)
                    status, _reason = cases.get(key, ("fail", "missing_case_status"))
                    if status in {"unsupported", "skip"}:
                        emit(
                            f"| {size}B | {status.upper()} | {status.upper()} | "
                            f"{status.upper()} | {status.upper()} | {status.upper()} |"
                        )
                        continue
                    metric_values = {
                        metric: metric_value_for_run(key, metric, run_index)
                        for metric in required_metrics
                    }
                    if any(math.isnan(value) for value in metric_values.values()):
                        emit(f"| {size}B | FAIL | FAIL | FAIL | FAIL | FAIL |")
                    else:
                        emit_case_row(pattern, size, metric_values)
                emit("")
            emit("median:")
        emit_table_header()
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            status, reason = cases.get(key, ("fail", "missing_case_status"))
            if status in {"unsupported", "skip"}:
                emit(
                    f"| {size}B | {status.upper()} | {status.upper()} | "
                    f"{status.upper()} | {status.upper()} | {status.upper()} |"
                )
                continue

            expected += 5
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in required_metrics}
            complete_case = all(len(rows[key].get(metric, [])) == runs for metric in required_metrics)
            if complete_case:
                actual += 5
                emit_case_row(pattern, size, metric_values)
                for metric in required_metrics:
                    result_lines.append(
                        f"RESULT,current,{pattern},{transport},{size},{metric},{fmt_metric(metric_values[metric])}"
                    )
            else:
                emit("| {}B | FAIL | FAIL | FAIL | FAIL | FAIL |".format(size))
                failures.append(f"{pattern} current {transport} {size}B: {reason or 'missing_result_lines'}")
        emit("")

for line in result_lines:
    emit(line)

emit("")
emit_effective_options("result")
emit("## Completion")
emit(f"- status: {'complete' if expected == actual else 'partial'}")
emit(f"- expected_result_lines: {expected}")
emit(f"- actual_result_lines: {actual}")
emit("")
emit("## Failures")
for failure in failures:
    emit(f"- {failure}")

text = "\n".join(lines) + "\n"
with open(report_path, "w", encoding="utf-8") as f:
    f.write(text)
if output_path:
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
sys.stdout.write(text)
sys.exit(0 if expected == actual else 1)
PY

prune_reports "${RESULTS_DIR}"
