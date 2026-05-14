#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${PROJECT_DIR}/../.." && pwd)"
CORE_BUILD_DIR="${REPO_DIR}/core/build"
CORE_LIB_DIR="${CORE_BUILD_DIR}/lib"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so"
PERF_REPORT_PY="${REPO_DIR}/bindings/python/perf/perf_report.py"
START_SECONDS="$(date +%s)"
TOTAL_TIME_ENABLED=0

print_total_time() {
    local status="${1:-0}"
    if [[ "${TOTAL_TIME_ENABLED}" -ne 1 ]]; then
        return
    fi
    local elapsed
    elapsed=$(($(date +%s) - START_SECONDS))
    echo "Total benchmark time: ${elapsed}s (${elapsed}s, exit=${status})"
}
STREAM_BUILD_DIR="${SCRIPT_DIR}/build/stream-client"
STREAM_CLIENT=""
REUSE_BUILD=0
CLEAN_BUILD=0

source "$HOME/.cargo/env" 2>/dev/null || true
export RUSTFLAGS="${RUSTFLAGS:+${RUSTFLAGS} }-Awarnings"

PATTERN="ALL"
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-tcp,tls,ws,wss}"
RUNS="1"
CLIENTS="${PERF_MULTI_CLIENTS:-100}"
RUN_COOLDOWN_MS="${PERF_MULTI_RUN_COOLDOWN_MS:-3000}"
RESULTS_ROOT="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results}"
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
SNDBUF=""
RCVBUF=""
SNDTIMEO_MS="${PERF_MULTI_SNDTIMEO_MS:-200}"
RCVTIMEO_MS="${PERF_MULTI_RCVTIMEO_MS:-200}"
AUTO_HWM_PROFILE="${PERF_CTX_AUTO_HWM_PROFILE:-}"
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
ENV_MULTI_SNDBUF="${PERF_MULTI_SNDBUF:-}"
ENV_MULTI_RCVBUF="${PERF_MULTI_RCVBUF:-}"
EXPLICIT_CLIENTS=0
[[ -n "${PERF_MSG_SIZES+x}" ]] && EXPLICIT_MSG_SIZES=1
[[ -n "${PERF_MULTI_CLIENTS+x}" ]] && EXPLICIT_CLIENTS=1

is_uint() {
    local value="${1:-}"
    [[ "${value}" =~ ^[0-9]+$ ]]
}

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
  --buf SIZE
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
  --auto-hwm-profile NAME
  --results-dir PATH
  --results-tag NAME
  --output PATH

Notes:
  - MULTI_STREAM uses the shared perf_stream_client required by policy.
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
        --clients)     CLIENTS="$2"; EXPLICIT_CLIENTS=1; shift 2 ;;
        --build-dir)   BUILD_DIR="$2";   shift 2 ;;
        --io-threads) COMMON_IO_THREADS="$2"; shift 2 ;;
        --server-io-threads) SERVER_IO_THREADS="$2"; shift 2 ;;
        --client-io-threads) CLIENT_IO_THREADS="$2"; shift 2 ;;
        --hwm) HWM="$2"; shift 2 ;;
        --send-hwm) SEND_HWM="$2"; shift 2 ;;
        --recv-hwm) RECV_HWM="$2"; shift 2 ;;
        --buf) SNDBUF="$2"; RCVBUF="$2"; shift 2 ;;
        --sndbuf) SNDBUF="$2"; shift 2 ;;
        --rcvbuf) RCVBUF="$2"; shift 2 ;;
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
        --auto-hwm-profile) AUTO_HWM_PROFILE="$2"; shift 2 ;;
        --results-dir) RESULTS_ROOT="$2"; shift 2 ;;
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
REPORT_DIR="${RESULTS_ROOT}/multi/report"
RESULTS_FILE="${REPORT_DIR}/perf_rust_multi_${PLATFORM}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${REPORT_DIR}"

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

NOFILE_SKIP_REASON=""
ensure_nofile_limit() {
    local clients="${1:-}"
    NOFILE_SKIP_REASON=""

    if [[ "${PERF_SKIP_NOFILE_CHECK:-0}" == "1" ]]; then
        return 0
    fi
    if ! is_uint "${clients}"; then
        return 0
    fi

    local required=$(( clients * 3 + 4096 ))
    local soft
    local hard
    soft="$(ulimit -Sn 2>/dev/null || true)"
    hard="$(ulimit -Hn 2>/dev/null || true)"
    if [[ -z "${soft}" || -z "${hard}" ]]; then
        return 0
    fi
    if [[ "${soft}" == "unlimited" ]]; then
        return 0
    fi
    if ! is_uint "${soft}"; then
        return 0
    fi

    local soft_num="${soft}"
    local hard_num=-1
    if [[ "${hard}" == "unlimited" ]]; then
        hard_num=-1
    elif is_uint "${hard}"; then
        hard_num="${hard}"
    else
        hard_num="${soft_num}"
    fi

    if (( soft_num < required )); then
        local target="${required}"
        if (( hard_num >= 0 && target > hard_num )); then
            target="${hard_num}"
        fi
        if (( target > soft_num )); then
            ulimit -Sn "${target}" 2>/dev/null || true
            soft="$(ulimit -Sn 2>/dev/null || true)"
            if is_uint "${soft}"; then
                soft_num="${soft}"
            fi
        fi
    fi

    if (( soft_num >= required )); then
        return 0
    fi

    NOFILE_SKIP_REASON="clients=${clients},required=${required},soft=${soft},hard=${hard}"
    return 1
}

MEMORY_SKIP_REASON=""
memory_available_kb() {
    if [[ "${PERF_SKIP_MEMORY_CHECK:-0}" == "1" ]]; then
        printf '\n'
        return
    fi
    if [[ ! -r /proc/meminfo ]]; then
        printf '\n'
        return
    fi
    awk '/^MemAvailable:/ { print $2; found=1; exit } END { if (!found) print "" }' /proc/meminfo 2>/dev/null || true
}

prepare_core_runtime() {
    if [[ ! -f "${CORE_LIB}" ]]; then
        echo "core runtime not found: ${CORE_LIB}" >&2
        echo "Build core/build before running Rust perf." >&2
        exit 1
    fi
    local newer_source
    newer_source="$(
        find "${REPO_DIR}/core/src" "${REPO_DIR}/core/include" \
            -type f -newer "${CORE_LIB}" -print -quit 2>/dev/null || true
    )"
    if [[ -n "${newer_source}" ]]; then
        echo "Error: stale core runtime detected for bindings/rust/perf." >&2
        echo "  runtime: ${CORE_LIB}" >&2
        echo "  newer source: ${newer_source}" >&2
        echo "Rebuild core/build before running run_benchmarks_multi.sh." >&2
        exit 1
    fi
    echo "Perf core build dir: ${CORE_BUILD_DIR}"
    echo "Perf runtime libzlink: ${CORE_LIB}"
    export LD_LIBRARY_PATH="${CORE_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
}

resolve_memory_max_clients() {
    local available_kb
    available_kb="$(memory_available_kb)"
    if ! is_uint "${available_kb}"; then
        printf '\n'
        return
    fi

    local budget_pct
    local base_mb
    local per_client_kb
    budget_pct="${PERF_MULTI_MEMORY_BUDGET_PCT:-${PERF_MEMORY_BUDGET_PCT:-70}}"
    base_mb="${PERF_MULTI_MEMORY_BASE_MB:-${PERF_MEMORY_BASE_MB:-512}}"
    per_client_kb="${PERF_MULTI_MEMORY_PER_CLIENT_KB:-${PERF_MEMORY_PER_CLIENT_KB:-1024}}"
    if ! is_uint "${budget_pct}" || (( budget_pct < 1 || budget_pct > 95 )); then
        printf '\n'
        return
    fi
    if ! is_uint "${base_mb}" || ! is_uint "${per_client_kb}" || (( per_client_kb < 1 )); then
        printf '\n'
        return
    fi

    local usable_kb=$(( available_kb * budget_pct / 100 ))
    local base_kb=$(( base_mb * 1024 ))
    if (( usable_kb <= base_kb )); then
        printf '%s\n' "1"
        return
    fi

    local max_clients=$(( (usable_kb - base_kb) / per_client_kb ))
    if (( max_clients < 1 )); then
        max_clients=1
    fi
    printf '%s\n' "${max_clients}"
}

cap_default_clients_for_memory() {
    local clients="${1:-}"
    if [[ "${EXPLICIT_CLIENTS}" -eq 1 ]]; then
        printf '%s\n' "${clients}"
        return
    fi

    local max_clients
    max_clients="$(resolve_memory_max_clients)"
    if ! is_uint "${clients}" || ! is_uint "${max_clients}"; then
        printf '%s\n' "${clients}"
        return
    fi
    if (( clients > max_clients )); then
        printf '%s\n' "${max_clients}"
        return
    fi
    printf '%s\n' "${clients}"
}

ensure_memory_budget() {
    local clients="${1:-}"
    MEMORY_SKIP_REASON=""

    if [[ "${PERF_SKIP_MEMORY_CHECK:-0}" == "1" ]]; then
        return 0
    fi
    if ! is_uint "${clients}"; then
        return 0
    fi

    local max_clients
    max_clients="$(resolve_memory_max_clients)"
    if ! is_uint "${max_clients}"; then
        return 0
    fi
    if (( clients <= max_clients )); then
        return 0
    fi

    local available_kb
    local budget_pct
    local base_mb
    local per_client_kb
    available_kb="$(memory_available_kb)"
    budget_pct="${PERF_MULTI_MEMORY_BUDGET_PCT:-${PERF_MEMORY_BUDGET_PCT:-70}}"
    base_mb="${PERF_MULTI_MEMORY_BASE_MB:-${PERF_MEMORY_BASE_MB:-512}}"
    per_client_kb="${PERF_MULTI_MEMORY_PER_CLIENT_KB:-${PERF_MEMORY_PER_CLIENT_KB:-1024}}"
    MEMORY_SKIP_REASON="clients=${clients},max_clients=${max_clients},mem_available_kb=${available_kb},budget_pct=${budget_pct},base_mb=${base_mb},per_client_kb=${per_client_kb}"
    return 1
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
    "SPOT_SENDSEND",
    "STREAM",
}

if raw == "ALL":
    print("MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND,MULTI_STREAM")
    raise SystemExit(0)

items = []
for token in raw.split(","):
    value = token.strip()
    if not value:
        continue
    if value.startswith("MULTI_"):
        value = value[len("MULTI_"):]
    if value == "STREAMS":
        value = "STREAM"
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
        *) printf '%s' "4" ;;
    esac
}

default_hwm_for_pattern() {
    printf '%s' "1000"
}

ensure_shared_stream_client() {
    if [[ "${REUSE_BUILD}" -eq 1 ]]; then
        if [[ -z "${STREAM_CLIENT}" ]]; then
            STREAM_CLIENT="${STREAM_BUILD_DIR}/perf_stream_client"
        fi
        if [[ -z "${STREAM_CLIENT}" || ! -x "${STREAM_CLIENT}" ]]; then
            echo "shared stream client not found for --reuse-build: ${STREAM_CLIENT}" >&2
            exit 1
        fi
        return
    fi

    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        rm -rf "${STREAM_BUILD_DIR}"
    fi
    mkdir -p "${STREAM_BUILD_DIR}"
    local cxx_bin
    cxx_bin="${CXX:-g++}"
    "${cxx_bin}" \
        -O2 \
        -std=c++17 \
        -Wall \
        -Wextra \
        "${REPO_DIR}/bindings/c/perf/common/streamclient/perf_stream_client.cpp" \
        -o "${STREAM_BUILD_DIR}/perf_stream_client" \
        -pthread \
        -lssl \
        -lcrypto >/dev/null
    STREAM_CLIENT="${STREAM_BUILD_DIR}/perf_stream_client"
    if [[ -z "${STREAM_CLIENT}" || ! -x "${STREAM_CLIENT}" ]]; then
        echo "shared stream client binary not found after build: ${STREAM_BUILD_DIR}" >&2
        exit 1
    fi
}

if [[ -n "${BUILD_DIR}" ]]; then
    TARGET_DIR="${BUILD_DIR}/rust-multi"
    STREAM_BUILD_DIR="${BUILD_DIR}/stream-client"
else
    TARGET_DIR="${SCRIPT_DIR}/multi/target"
fi
BIN_DIR="${TARGET_DIR}/release"

export PERF_MULTI_DURATION_SECONDS="${DURATION}"
prepare_core_runtime
TOTAL_TIME_ENABLED=1
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
[[ -n "${SNDBUF:-${ENV_MULTI_SNDBUF}}" ]] && export PERF_MULTI_SNDBUF="${SNDBUF:-${ENV_MULTI_SNDBUF}}"
[[ -n "${RCVBUF:-${ENV_MULTI_RCVBUF}}" ]] && export PERF_MULTI_RCVBUF="${RCVBUF:-${ENV_MULTI_RCVBUF}}"
[[ -n "${AUTO_HWM_PROFILE}" ]] && export PERF_CTX_AUTO_HWM_PROFILE="${AUTO_HWM_PROFILE}"

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
    ensure_shared_stream_client
fi

IFS=',' read -ra TRANSPORT_LIST <<< "${TRANSPORTS}"
ONE_WAY_CLIENT_READY_TIMEOUT=10
SERVER_READY_TIMEOUT_SECONDS=$(( (SERVER_READY_TIMEOUT_MS + 999) / 1000 ))
SERVER_SHUTDOWN_TIMEOUT_SECONDS=$(( (SERVER_SHUTDOWN_TIMEOUT_MS + 999) / 1000 ))

TMP_METRICS="$(mktemp)"
TMP_CASES="$(mktemp)"
cleanup() {
    local status=$?
    rm -f "${TMP_METRICS}" "${TMP_CASES}"
    print_total_time "${status}"
}
trap cleanup EXIT
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

    if [[ "${pattern}" == "MULTI_SPOT" || "${pattern}" == "MULTI_SPOT_REQREP" || "${pattern}" == "MULTI_SPOT_SENDSEND" ]] \
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
        PATTERN_CLIENTS="$(cap_default_clients_for_memory "${PATTERN_CLIENTS}")"
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
            MULTI_SPOT_SENDSEND)
                SERVER_BIN="${BIN_DIR}/perf_multi_spot_sendsend_server"
                CLIENT_BIN="${BIN_DIR}/perf_multi_spot_sendsend_client" ;;
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
                if ! ensure_nofile_limit "${PATTERN_CLIENTS}"; then
                    printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "skip" "nofile_guard:${NOFILE_SKIP_REASON}" >> "${TMP_CASES}"
                    continue
                fi
                if ! ensure_memory_budget "${PATTERN_CLIENTS}"; then
                    printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "skip" "memory_guard:${MEMORY_SKIP_REASON}" >> "${TMP_CASES}"
                    continue
                fi
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
                export PERF_MULTI_MSG_UNIT_BYTES="${size}"
                export PERF_MULTI_HWM="${HWM:-${ENV_MULTI_HWM:-${pattern_default_hwm}}}"
                export PERF_MULTI_SNDHWM="${SEND_HWM:-${ENV_MULTI_SNDHWM:-${PERF_MULTI_HWM}}}"
                export PERF_MULTI_RCVHWM="${RECV_HWM:-${ENV_MULTI_RCVHWM:-${PERF_MULTI_HWM}}}"
                if [[ "${pat}" == "MULTI_STREAM" ]]; then
                    export PERF_RECV_MODE="recv"
                    export PERF_DURATION_SECONDS="${DURATION}"
                    export PERF_WARMUP_SECONDS="1"
                fi
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
                if [[ "${pat}" == "MULTI_SPOT" || "${pat}" == "MULTI_SPOT_REQREP" || "${pat}" == "MULTI_SPOT_SENDSEND" ]]; then
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
                elif [[ "${pat}" == "MULTI_SPOT" || "${pat}" == "MULTI_SPOT_REQREP" || "${pat}" == "MULTI_SPOT_SENDSEND" ]]; then
                    CLIENT_OUT="$(mktemp)"
                    CLIENT_ERR="$(mktemp)"
                    CLIENT_FIFO="$(mktemp -u)"
                    mkfifo "${CLIENT_FIFO}"
                    CLIENT_ENDPOINT="${ENDPOINT},${CONTROL_ENDPOINT}"
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
                        CLIENT_CONTROL_CONNECTED="$(wait_for_file_prefix "${SRV_OUT}" "CONTROL_CONNECTED," "${ONE_WAY_CLIENT_READY_TIMEOUT}" || true)"
                        if [[ "${CLIENT_CONTROL_CONNECTED}" != "CONTROL_CONNECTED,${CLIENT_CONTROL_ENDPOINT}" ]]; then
                            case_status="fail"
                            case_reason="control_connect_timeout"
                        else
                            printf '%s\n' "${CLIENT_CONTROL_CONNECTED}" >&"${CLIENT_CONTROL_FD}" || true
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
                        --warmup 1 \
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
TOTAL_CASES="$(wc -l < "${TMP_CASES}" | tr -d ' ')"
NON_SKIP_CASES="$(awk -F',' '$4 != "skip" {count++} END {print count+0}' "${TMP_CASES}")"
if [[ "${TOTAL_CASES}" -gt 0 && "${NON_SKIP_CASES}" -eq 0 ]]; then
    python3 - "${TMP_CASES}" "${OUTPUT_FILE}" <<'PY'
import csv
import sys

cases_path, output_path = sys.argv[1:]
lines = ["## Skips"]
with open(cases_path, newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    for pattern, transport, size, status, reason in reader:
        if status != "skip":
            continue
        lines.append(f"- {pattern} {transport} {size}B: {reason}")
text = "\n".join(lines) + "\n"
if output_path:
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
sys.stdout.write(text)
PY
    exit 0
fi
python3 - "${TMP_METRICS}" "${TMP_CASES}" "${RESULTS_FILE}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${CLIENTS}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${OUTPUT_FILE}" "${PIN_CPU}" \
  "${COMMON_IO_THREADS}" "${SERVER_IO_THREADS}" "${CLIENT_IO_THREADS}" "${HWM}" "${SEND_HWM}" \
  "${RECV_HWM}" "${SNDTIMEO_MS}" "${RCVTIMEO_MS}" "${RUN_COOLDOWN_MS}" \
  "${TRANSPORT_TRANSITION_MS}" "${PATTERN_TRANSITION_MS}" "${SECONDS}" \
  "${PERF_REPORT_PY}" <<'PY'
import csv
import math
import os
import platform
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime

metrics_path, cases_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, clients, runs, duration, results_tag, output_path, pin_cpu, common_io_threads, server_io_threads, client_io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, run_cooldown_ms, transport_transition_ms, pattern_transition_ms, elapsed_seconds, perf_report_path = sys.argv[1:]
sys.path.insert(0, os.path.dirname(perf_report_path))
from perf_report import multi_auto_hwm_lines
runs = int(runs)
required_metrics = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
result_metrics = ["bandwidth", "latency", "latency_p95", "latency_p99", "throughput"]
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
    return "N/A" if math.isnan(value) else f"{value / 1000.0:.3f}"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} MB/s"

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} ms"

def fmt_metric(value):
    return "N/A" if math.isnan(value) else f"{value:.3f}"

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
    emit(f"- duration_seconds: {duration}")
    emit(f"- clients: {clients}")
    emit("- default_clients: 100")
    emit("- default_stream_clients: 10000")
    emit("- service_clients: auto")
    emit(f"- server_io_threads: {server_io_threads or common_io_threads or '4 (default)'}")
    emit(f"- client_io_threads: {client_io_threads or common_io_threads or '4 (default)'}")
    emit(f"- hwm: {hwm or 'auto-hwm'}")
    emit(f"- sndhwm: {send_hwm or hwm or 'auto-hwm'}")
    emit(f"- rcvhwm: {recv_hwm or hwm or 'auto-hwm'}")
    emit("- sndbuf: auto-hwm")
    emit("- rcvbuf: auto-hwm")
    emit("- ctx_auto_hwm_enable: 1")
    emit("- ctx_auto_hwm_profile: balanced")
    emit(f"- sndtimeo_ms: {sndtimeo_ms}")
    emit(f"- rcvtimeo_ms: {rcvtimeo_ms}")
    emit("- connect_concurrency: 128 (default)")
    emit("- connect_ready_timeout_ms: 5000")
    emit("- monitor_hwm: 1000")
    emit("- server_ready_timeout_ms: 10000")
    emit("- server_shutdown_timeout_ms: 5000")
    emit("- server_bind_port: 0")
    emit(f"- transport_transition_ms: {transport_transition_ms}")
    emit(f"- pattern_transition_ms: {pattern_transition_ms}")
    emit("- lat_timeout_ms: 5000")
    emit("- stream_non_tcp_clients_max: 10000")
    emit("- disable_resource_metrics: 0")
    emit("- timeout_seconds: auto")
    if section == "result":
        emit("")

def cpu_name():
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"

def git_commit():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return "unknown"

try:
    load_avg = " ".join(f"{value:.2f}" for value in os.getloadavg())
except OSError:
    load_avg = "unknown"

emit(f"META,os,{platform.system()} {platform.release()}")
emit(f"META,cpu,{cpu_name()}")
emit(f"META,cores,{os.cpu_count() or 0}")
emit("META,build,Release")
emit(f"META,commit,{git_commit()}")
emit(f"META,timestamp,{datetime.now().astimezone().isoformat(timespec='seconds')}")
emit(f"META,load_avg,{load_avg}")
emit(f"META,runs,{runs}")
emit(f"META,clients,{clients}")
emit("")
emit_effective_options("start")

def pattern_direction(pattern):
    return "echo" if pattern in {
        "MULTI_DEALER_ROUTER",
        "MULTI_ROUTER_ROUTER",
        "MULTI_STREAM",
        "MULTI_SPOT_REQREP",
        "MULTI_SPOT_SENDSEND",
    } else "one-way"

def rate_unit(pattern):
    return "Kops/s" if pattern_direction(pattern) == "echo" else "Kmsg/s"

def emit_table_header():
    emit("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |")
    emit("|----------|--------------------|----------------|---------------|---------------|---------------|")

def metric_value_for_run(key, metric, run_index):
    values = rows[key].get(metric, [])
    if run_index < len(values):
        return values[run_index]
    return math.nan

def emit_case_row(pattern, size, metric_values):
    throughput = f"{fmt_rate(metric_values['throughput']):>8} {rate_unit(pattern)}"
    emit(
        f"      | {str(size) + 'B':<8} | {throughput:>16} | "
        f"{metric_values['bandwidth']:10.3f} MB/s | "
        f"{fmt_latency_ms(metric_values['latency']):>12} | "
        f"{fmt_latency_ms(metric_values['latency_p95']):>12} | "
        f"{fmt_latency_ms(metric_values['latency_p99']):>12} |"
    )

for pattern_index, pattern in enumerate(patterns):
    if pattern_index:
        emit("===============================================================================")
        emit("")
    emit(f"## PATTERN: {pattern} ({pattern_direction(pattern)})")
    emit(f"  > Benchmarking current for {pattern}...")
    for transport in pattern_transports[pattern]:
        emit(f"    Testing {transport}:")
        if runs > 1:
            for run_index in range(runs):
                emit(f"      run {run_index + 1}/{runs}:")
                for header in ("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |", "|----------|--------------------|----------------|---------------|---------------|---------------|"):
                    emit(f"        {header}")
                for size in pattern_sizes[pattern]:
                    key = (pattern, transport, size)
                    status, _reason = cases.get(key, ("fail", "missing_case_status"))
                    if status == "unsupported":
                        emit(
                            f"        | {size}B | UNSUPPORTED | UNSUPPORTED | "
                            f"UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |"
                        )
                        continue
                    if status == "skip":
                        emit(f"        | {size}B | FAIL | FAIL | FAIL | FAIL | FAIL |")
                        continue
                    metric_values = {
                        metric: metric_value_for_run(key, metric, run_index)
                        for metric in required_metrics
                    }
                    if any(math.isnan(value) for value in metric_values.values()):
                        emit(f"        | {size}B | FAIL | FAIL | FAIL | FAIL | FAIL |")
                    else:
                        emit_case_row(pattern, size, metric_values)
            emit("      median:")
        for header in ("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |", "|----------|--------------------|----------------|---------------|---------------|---------------|"):
            emit(f"      {header}")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            emit(f"    Testing {transport} | {size}B:")
            status, reason = cases.get(key, ("fail", "missing_case_status"))
            if status == "unsupported":
                emit(
                    f"      | {size}B | UNSUPPORTED | UNSUPPORTED | "
                    f"UNSUPPORTED | UNSUPPORTED | UNSUPPORTED |"
                )
                continue
            if status == "skip":
                emit("      | {}B | FAIL | FAIL | FAIL | FAIL | FAIL |".format(size))
                continue

            expected += 5
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in required_metrics}
            complete_case = all(len(rows[key].get(metric, [])) == runs for metric in required_metrics)
            if complete_case:
                actual += 5
                emit_case_row(pattern, size, metric_values)
                for metric in result_metrics:
                    result_lines.append(
                        f"RESULT,current,{pattern},{transport},{size},{metric},{fmt_metric(metric_values[metric])}"
                    )
            else:
                emit("      | {}B | FAIL | FAIL | FAIL | FAIL | FAIL |".format(size))
                failures.append(f"{pattern} current {transport} {size}B: {reason or 'missing_result_lines'}")
        emit(f"    Testing {transport}: Done")
        for line in multi_auto_hwm_lines(pattern, pattern_sizes[pattern]):
            emit(line)
    emit("")

emit_effective_options("result")
emit("## Result Data")
for line in result_lines:
    emit(line)
emit("")
emit("## Completion")
emit(f"- success: {actual // 5}")
emit(f"- unsupported: {sum(1 for status, _reason in cases.values() if status == 'unsupported')}")
emit(f"- skip: {sum(1 for status, _reason in cases.values() if status == 'skip')}")
emit(f"- fail: {len(failures)}")
emit(f"- status: {'complete' if expected == actual else 'partial'}")
emit(f"- expected_result_lines: {expected}")
emit(f"- actual_result_lines: {actual}")
if failures:
    emit("")
    emit("## Failures")
    for failure in failures:
        emit(f"- {failure}")
status = 'complete' if expected == actual else 'partial'
emit("")
emit(f"Saved result file: {report_path} (status={status})")

text = "\n".join(lines) + "\n"
with open(report_path, "w", encoding="utf-8") as f:
    f.write(text)
if output_path:
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
sys.stdout.write(text)
sys.exit(0 if expected == actual else 1)
PY

prune_reports "${REPORT_DIR}"
