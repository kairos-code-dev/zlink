#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${PROJECT_DIR}/../.." && pwd)"
STREAM_CLIENT="${REPO_DIR}/core/build/bin/perf_stream_client"
CORE_BUILD_DIR="${REPO_DIR}/core/build"
REUSE_BUILD=0

source "$HOME/.cargo/env" 2>/dev/null || true

PATTERN="ALL"
DURATION="${PERF_MULTI_DURATION_SECONDS:-5}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-tcp,tls,ws,wss}"
RUNS="${PERF_RUNS:-1}"
CLIENTS="${PERF_MULTI_CLIENTS:-100}"
RESULTS_DIR="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results/multi/report}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"
OUTPUT_FILE=""

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
        --msg-sizes)   MSG_SIZES="$2";   shift 2 ;;
        --transports)  TRANSPORTS="$2";  shift 2 ;;
        --runs)        RUNS="$2";        shift 2 ;;
        --clients)     CLIENTS="$2";     shift 2 ;;
        --results-dir) RESULTS_DIR="$2"; shift 2 ;;
        --results-tag) RESULTS_TAG="$2"; shift 2 ;;
        --output)      OUTPUT_FILE="$2"; shift 2 ;;
        --reuse-build) REUSE_BUILD=1; shift ;;
        --clean-build)
            echo "--clean-build is not supported for the shared core stream client in this runner" >&2
            exit 1
            ;;
        --build-dir|--io-threads|--server-io-threads|--client-io-threads|--hwm|--send-hwm|--recv-hwm|--sndbuf|--rcvbuf|--sndtimeo|--rcvtimeo|--send-timeout-ms|--recv-timeout-ms|--connect-concurrency|--transport-transition-ms|--pattern-transition-ms|--server-ready-timeout-ms|--connect-ready-timeout-ms|--monitor-hwm|--server-shutdown-timeout-ms|--server-bind-port)
            shift 2 ;;
        --pin-cpu) shift ;;
        *)             echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

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
    "STREAM",
}

if raw == "ALL":
    print("MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,MULTI_SPOT,MULTI_STREAM")
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

ensure_core_stream_client() {
    if [[ "${REUSE_BUILD}" -eq 1 ]]; then
        if [[ ! -x "${STREAM_CLIENT}" ]]; then
            echo "shared stream client not found for --reuse-build: ${STREAM_CLIENT}" >&2
            exit 1
        fi
        return
    fi

    cmake -S "${REPO_DIR}" -B "${CORE_BUILD_DIR}" \
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
    cmake --build "${CORE_BUILD_DIR}" --target perf_stream_client >/dev/null
}

export PERF_MULTI_CLIENTS="${CLIENTS}"
export PERF_MULTI_DURATION_SECONDS="${DURATION}"
export LD_LIBRARY_PATH="${PROJECT_DIR}/native/linux-x86_64:${LD_LIBRARY_PATH:-}"

(cd "${SCRIPT_DIR}/multi" && cargo build --release --quiet)
BIN_DIR="${SCRIPT_DIR}/multi/target/release"

PATTERN="$(normalize_patterns "${PATTERN}")"
IFS=',' read -ra PATTERNS <<< "${PATTERN}"
if printf '%s\n' "${PATTERNS[@]}" | grep -qx 'MULTI_STREAM'; then
    ensure_core_stream_client
fi

IFS=',' read -ra SIZE_LIST <<< "${MSG_SIZES}"
IFS=',' read -ra TRANSPORT_LIST <<< "${TRANSPORTS}"

SERVER_READY_TIMEOUT=10
SERVER_SHUTDOWN_TIMEOUT=5
CLIENT_TIMEOUT_SECONDS=$((DURATION + 10))
ONE_WAY_CLIENT_READY_TIMEOUT=10

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
        if wait_for_pid "${pid}" "${SERVER_SHUTDOWN_TIMEOUT}"; then
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
                echo "UNSUPPORTED,rust,${pat},unknown"
                continue ;;
        esac

        for transport in "${TRANSPORT_LIST[@]}"; do
            for size in "${SIZE_LIST[@]}"; do
                case_status="success"
                case_reason=""
                SRV_OUT=$(mktemp)
                SERVER_FIFO="$(mktemp -u)"
                mkfifo "${SERVER_FIFO}"
                "${SERVER_BIN}" "${transport}" "${size}" < "${SERVER_FIFO}" > "${SRV_OUT}" 2>&1 &
                SERVER_PID=$!
                exec {SERVER_CONTROL_FD}> "${SERVER_FIFO}"
                rm -f "${SERVER_FIFO}"

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
                    case_status="fail"
                    case_reason="server_ready_timeout"
                    shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                    rm -f "${SRV_OUT}"
                    printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
                    continue
                fi

                if [[ "${pat}" == "MULTI_DEALER_DEALER" || "${pat}" == "MULTI_PUBSUB" ]]; then
                    CLIENT_OUT="$(mktemp)"
                    CLIENT_ERR="$(mktemp)"
                    CLIENT_FIFO="$(mktemp -u)"
                    mkfifo "${CLIENT_FIFO}"
                    "${CLIENT_BIN}" "${transport}" "${size}" "${ENDPOINT}" < "${CLIENT_FIFO}" > "${CLIENT_OUT}" 2> "${CLIENT_ERR}" &
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
                        OUTPUT="$(cat "${CLIENT_OUT}")"
                    fi
                    if [[ -s "${CLIENT_ERR}" ]]; then
                        if [[ -n "${OUTPUT}" ]]; then
                            OUTPUT+=$'\n'
                        fi
                        OUTPUT+="$(cat "${CLIENT_ERR}")"
                    fi
                    rm -f "${CLIENT_OUT}" "${CLIENT_ERR}"
                elif [[ "${pat}" == "MULTI_STREAM" ]]; then
                    if ! OUTPUT="$(timeout "${CLIENT_TIMEOUT_SECONDS}s" "${STREAM_CLIENT}" \
                        --transport tcp \
                        --pattern STREAM \
                        --sizes "${size}" \
                        --runs 1 \
                        --duration "${DURATION}" \
                        --ccu "${CLIENTS}" \
                        --io-threads 4 \
                        --print-perf-result 1 \
                        --send-stop-token 0 \
                        --endpoint "${ENDPOINT}" 2>&1)"; then
                        case_status="fail"
                        case_reason="binary_exit_or_timeout"
                    fi
                else
                    if ! OUTPUT="$(timeout "${CLIENT_TIMEOUT_SECONDS}s" "${CLIENT_BIN}" "${transport}" "${size}" "${ENDPOINT}" 2>&1)"; then
                        case_status="fail"
                        case_reason="binary_exit_or_timeout"
                    fi
                fi

                shutdown_server "${SERVER_PID}" "${SERVER_CONTROL_FD}"
                rm -f "${SRV_OUT}"
                if [[ "${case_status}" == "success" ]]; then
                    unsupported_line="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^UNSUPPORTED,/ {print; exit}')"
                    if [[ -n "${unsupported_line}" ]]; then
                        case_status="unsupported"
                        case_reason="${unsupported_line}"
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
        done
    done
done

python3 - "${TMP_METRICS}" "${TMP_CASES}" "${RESULTS_FILE}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${CLIENTS}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${OUTPUT_FILE}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, cases_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, clients, runs, duration, results_tag, output_path = sys.argv[1:]
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
    return "N/A" if math.isnan(value) else f"{value / 1000.0:.2f} Kmsg/s"

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
    emit("- pin_cpu: off")
    emit(f"- duration_seconds: {duration}")
    if results_tag:
        emit(f"- results_tag: {results_tag}")
    emit("")

emit_effective_options("start")
emit("===============================================================================")
emit("")

for pattern_index, pattern in enumerate(patterns):
    if pattern_index:
        emit("===============================================================================")
        emit("")
    emit(f"## PATTERN: {pattern}")
    emit("")
    for transport in pattern_transports[pattern]:
        emit(f"### Transport: {transport}")
        emit("| Size | Throughput | Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) |")
        emit("|------|------------|-----------|--------------|-------------|-------------|")
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
                emit(
                    f"| {size}B | {fmt_rate(metric_values['throughput'])} | "
                    f"{fmt_bandwidth(metric_values['bandwidth'])} | "
                    f"{fmt_latency_ms(metric_values['latency'])} | "
                    f"{fmt_latency_ms(metric_values['latency_p95'])} | "
                    f"{fmt_latency_ms(metric_values['latency_p99'])} |"
                )
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
PY

prune_reports "${RESULTS_DIR}"
