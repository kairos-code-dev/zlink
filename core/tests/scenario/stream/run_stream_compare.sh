#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

LOCK_FILE="/tmp/run_stream_compare.lock"
LOCK_FD=9
exec {LOCK_FD}>"${LOCK_FILE}"
if ! flock -n "${LOCK_FD}"; then
    echo "[$(date +'%F %T')] another run_stream_compare.sh is already running." >&2
    exit 1
fi

usage()
{
    cat <<'USAGE'
Usage:
  run_stream_compare.sh [options]

Options:
  --stack <asio|cppserver|dotnet|zlink|cgdk10|all>
  --size <64|1024|65536|all>
  --ccu <N>
  --duration <sec>
  --repeats <N>
  --inflight <N>
  --client-io-threads <N>
  --server-io-threads <N>
  --max-attempts <N>
  --retry-delay <sec>
  --case-gap <sec>
  --server-start-timeout <sec>
  --port-scan-limit <N>
  --final-1024         preset: stack=all size=1024 ccu=10000 inflight=1 repeats=3
  -h, --help

Examples:
  ./run_stream_compare.sh
  ./run_stream_compare.sh --stack asio --size 1024 --ccu 10000 --duration 5 --repeats 3 --inflight 1
  ./run_stream_compare.sh --stack cppserver --size 1024 --client-io-threads 1 --server-io-threads 8
  ./run_stream_compare.sh --final-1024
USAGE
}

TARGET_STACK="all"
TARGET_SIZE="all"
CCU=10000
DURATION=5
REPEATS=3
INFLIGHT=1
CLIENT_IO_THREADS=4
SERVER_IO_THREADS=2
MAX_ATTEMPTS_PER_CASE=10
RETRY_DELAY_SEC=2
CASE_GAP_SEC=1
SERVER_START_TIMEOUT=40
PORT_SCAN_LIMIT=5000
FINAL_1024=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stack)
            TARGET_STACK="${2:-}"
            shift 2
            ;;
        --size)
            TARGET_SIZE="${2:-}"
            shift 2
            ;;
        --ccu)
            CCU="${2:-}"
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
        --max-attempts)
            MAX_ATTEMPTS_PER_CASE="${2:-}"
            shift 2
            ;;
        --retry-delay)
            RETRY_DELAY_SEC="${2:-}"
            shift 2
            ;;
        --case-gap)
            CASE_GAP_SEC="${2:-}"
            shift 2
            ;;
        --server-start-timeout)
            SERVER_START_TIMEOUT="${2:-}"
            shift 2
            ;;
        --port-scan-limit)
            PORT_SCAN_LIMIT="${2:-}"
            shift 2
            ;;
        --final-1024)
            FINAL_1024=1
            shift
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

if [[ "${FINAL_1024}" == "1" ]]; then
    TARGET_STACK="all"
    TARGET_SIZE="1024"
    CCU=10000
    DURATION=5
    REPEATS=3
    INFLIGHT=1
fi

HOST="${HOST:-127.0.0.1}"
DEFAULT_BASE_PORT="$((20000 + (RANDOM % 2000)))"
BASE_PORT="${BASE_PORT:-${DEFAULT_BASE_PORT}}"
if [[ -n "${IO_THREADS:-}" ]]; then
    CLIENT_IO_THREADS="${IO_THREADS}"
    SERVER_IO_THREADS="${IO_THREADS}"
fi

SNDBUF="${SNDBUF:-1048576}"
RCVBUF="${RCVBUF:-1048576}"
BACKLOG="${BACKLOG:-32768}"
TCP_NODELAY="${TCP_NODELAY:-1}"

STACKS_ALL=(asio cppserver dotnet zlink cgdk10)
SIZES_ALL=(64 1024 65536)
RUN_STACKS=()
RUN_SIZES=()

validate_int()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value < 1 )); then
        echo "invalid ${name}: ${value}" >&2
        exit 2
    fi
}

validate_bool01()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[01]$ ]]; then
        echo "invalid ${name}: ${value} (expected 0 or 1)" >&2
        exit 2
    fi
}

validate_int "--ccu" "${CCU}"
validate_int "--duration" "${DURATION}"
validate_int "--repeats" "${REPEATS}"
validate_int "--inflight" "${INFLIGHT}"
validate_int "--client-io-threads" "${CLIENT_IO_THREADS}"
validate_int "--server-io-threads" "${SERVER_IO_THREADS}"
validate_int "--max-attempts" "${MAX_ATTEMPTS_PER_CASE}"
validate_int "--retry-delay" "${RETRY_DELAY_SEC}"
validate_int "--case-gap" "${CASE_GAP_SEC}"
validate_int "--server-start-timeout" "${SERVER_START_TIMEOUT}"
validate_int "--port-scan-limit" "${PORT_SCAN_LIMIT}"
validate_int "BASE_PORT" "${BASE_PORT}"
validate_int "SNDBUF" "${SNDBUF}"
validate_int "RCVBUF" "${RCVBUF}"
validate_int "BACKLOG" "${BACKLOG}"
validate_bool01 "TCP_NODELAY" "${TCP_NODELAY}"

case "${TARGET_STACK}" in
    all)
        RUN_STACKS=("${STACKS_ALL[@]}")
        ;;
    asio|cppserver|dotnet|zlink|cgdk10)
        RUN_STACKS=("${TARGET_STACK}")
        ;;
    *)
        echo "invalid --stack: ${TARGET_STACK}" >&2
        exit 2
        ;;
esac

case "${TARGET_SIZE}" in
    all)
        RUN_SIZES=("${SIZES_ALL[@]}")
        ;;
    64|1024|65536)
        RUN_SIZES=("${TARGET_SIZE}")
        ;;
    *)
        echo "invalid --size: ${TARGET_SIZE}" >&2
        exit 2
        ;;
esac

RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/result/$(date +%Y%m%d_%H%M%S)}"
LOG_DIR="${RESULT_DIR}/logs"
SCENARIO_LOG="${RESULT_DIR}/scenario.log"
METRICS_CSV="${RESULT_DIR}/metrics.csv"
SUMMARY_JSON="${RESULT_DIR}/summary.json"
COMPARISON_MD="${RESULT_DIR}/comparison.md"

CLIENT_BIN="${BUILD_DIR}/bin/test_scenario_stream_client"
ASIO_BIN="${BUILD_DIR}/bin/test_scenario_stream_asio"
ZLINK_BIN="${BUILD_DIR}/bin/test_scenario_stream_zlink"

CPPSERVER_SRC_DIR="${SCRIPT_DIR}/cppserver/upstream"
CPPSERVER_BUILD_DIR="${CPPSERVER_SRC_DIR}/build-stream"
CPPSERVER_BIN="${CPPSERVER_BUILD_DIR}/cppserver-performance-stream_fixed_server"
CPPSERVER_UPSTREAM_ENTRY="${CPPSERVER_SRC_DIR}/performance/stream_fixed_server.cpp"

DOTNET_PROJECT="${SCRIPT_DIR}/dotnet/StreamServer.csproj"
DOTNET_OUT_DIR="${SCRIPT_DIR}/dotnet/bin/Release/stream-bench"
DOTNET_DLL="${DOTNET_OUT_DIR}/StreamServer.dll"

CGDK_REPO_URL="${CGDK_REPO_URL:-https://github.com/CGLabs/CGDK10.Cpp.git}"
CGDK_ROOT_DIR="${SCRIPT_DIR}/cgdk10"
CGDK_SRC_DIR="${CGDK_ROOT_DIR}/upstream/CGDK10.Cpp"
CGDK_BUILD_DIR="${CGDK_ROOT_DIR}/build"
CGDK_BIN="${CGDK_BUILD_DIR}/test_scenario_stream_cgdk10"
CGDK_SERVER_SRC="${CGDK_ROOT_DIR}/test_scenario_stream_cgdk10.cpp"
CGDK_LIB_DIR="${CGDK_SRC_DIR}/lib/cgdk/sdk10/ubuntu"

ACTIVE_SERVER_PID=""
ACTIVE_SERVER_STACK=""
PORT_CURSOR="${BASE_PORT}"
ALLOCATED_PORT=""
FAILED_CASES=0

log()
{
    mkdir -p "${RESULT_DIR}" "${LOG_DIR}"
    echo "[$(date +'%F %T')] $*" | tee -a "${SCENARIO_LOG}"
}

log_kernel_limits()
{
    if [[ -r /proc/sys/net/core/somaxconn ]]; then
        local somaxconn
        somaxconn="$(cat /proc/sys/net/core/somaxconn)"
        if [[ "${somaxconn}" =~ ^[0-9]+$ ]] && (( BACKLOG > somaxconn )); then
            log "warning: requested backlog=${BACKLOG} exceeds somaxconn=${somaxconn}"
        fi
    fi

    if [[ -r /proc/sys/net/core/rmem_max ]]; then
        local rmem_max
        rmem_max="$(cat /proc/sys/net/core/rmem_max)"
        if [[ "${rmem_max}" =~ ^[0-9]+$ ]] && (( RCVBUF > rmem_max )); then
            log "warning: requested rcvbuf=${RCVBUF} exceeds rmem_max=${rmem_max}"
        fi
    fi

    if [[ -r /proc/sys/net/core/wmem_max ]]; then
        local wmem_max
        wmem_max="$(cat /proc/sys/net/core/wmem_max)"
        if [[ "${wmem_max}" =~ ^[0-9]+$ ]] && (( SNDBUF > wmem_max )); then
            log "warning: requested sndbuf=${SNDBUF} exceeds wmem_max=${wmem_max}"
        fi
    fi
}

wait_for_port()
{
    local host="$1"
    local port="$2"
    local timeout_s="${3:-20}"
    local start_ts
    start_ts="$(date +%s)"

    while true; do
        if (echo >/dev/tcp/"${host}"/"${port}") >/dev/null 2>&1; then
            return 0
        fi

        local now_ts
        now_ts="$(date +%s)"
        if (( now_ts - start_ts >= timeout_s )); then
            return 1
        fi
        sleep 0.05
    done
}

can_bind_port()
{
    local host="$1"
    local port="$2"

    python3 - "${host}" "${port}" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    s.bind((host, port))
except OSError:
    sys.exit(1)
finally:
    s.close()
sys.exit(0)
PY
}

allocate_free_port()
{
    local host="$1"
    local scan_left="${PORT_SCAN_LIMIT}"
    ALLOCATED_PORT=""

    while (( scan_left > 0 )); do
        local candidate="${PORT_CURSOR}"
        PORT_CURSOR="$((PORT_CURSOR + 1))"
        if can_bind_port "${host}" "${candidate}"; then
            ALLOCATED_PORT="${candidate}"
            return 0
        fi
        scan_left="$((scan_left - 1))"
    done

    return 1
}

stop_active_server()
{
    if [[ -z "${ACTIVE_SERVER_PID}" ]]; then
        return
    fi

    if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
        if [[ "${ACTIVE_SERVER_STACK}" == "cgdk10" ]]; then
            kill -USR1 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
            sleep 0.3
        else
            kill -INT "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
            sleep 0.3
        fi

        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
            sleep 0.3
        fi

        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill -9 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        fi
    fi

    wait "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
    ACTIVE_SERVER_PID=""
    ACTIVE_SERVER_STACK=""
}

trap stop_active_server EXIT

ensure_cgdk_upstream()
{
    if [[ -d "${CGDK_SRC_DIR}/.git" ]]; then
        return
    fi
    mkdir -p "$(dirname "${CGDK_SRC_DIR}")"
    rm -rf "${CGDK_SRC_DIR}"
    git clone --depth 1 "${CGDK_REPO_URL}" "${CGDK_SRC_DIR}" >/dev/null 2>&1
}

build_cgdk_server()
{
    ensure_cgdk_upstream
    mkdir -p "${CGDK_BUILD_DIR}"

    if [[ ! -d "${CGDK_LIB_DIR}" ]]; then
        log "cgdk10 lib dir not found: ${CGDK_LIB_DIR}"
        return 1
    fi

    g++ \
        -I"${CGDK_SRC_DIR}/include" \
        -DC_FLAGS \
        -fexceptions \
        -std=c++20 \
        -DNDEBUG \
        -O3 \
        -Wall \
        -Wextra \
        -std=gnu++20 \
        "${CGDK_SERVER_SRC}" \
        -o "${CGDK_BIN}" \
        -L"${CGDK_LIB_DIR}" \
        -Wl,-rpath,"${CGDK_LIB_DIR}" \
        -lCGDK10.net.socket_linux.Release \
        -lCGDK10.system.object_linux.Release \
        -lrt \
        -lpthread \
        >/dev/null 2>&1
}

build_selected()
{
    log "build stream client"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null
    cmake --build "${BUILD_DIR}" --target test_scenario_stream_client -j"$(nproc)" >/dev/null

    local stack
    for stack in "${RUN_STACKS[@]}"; do
        case "${stack}" in
            asio)
                log "build asio server"
                cmake --build "${BUILD_DIR}" --target test_scenario_stream_asio -j"$(nproc)" >/dev/null
                ;;
            zlink)
                log "build zlink server"
                cmake --build "${BUILD_DIR}" --target test_scenario_stream_zlink -j"$(nproc)" >/dev/null
                ;;
            cppserver)
                log "build cppserver server"
                cat >"${CPPSERVER_UPSTREAM_ENTRY}" <<'CPP'
#include "../../test_scenario_stream_cppserver.cpp"
CPP
                cmake -S "${CPPSERVER_SRC_DIR}" -B "${CPPSERVER_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null
                cmake --build "${CPPSERVER_BUILD_DIR}" --target cppserver-performance-stream_fixed_server -j"$(nproc)" >/dev/null
                ;;
            dotnet)
                log "build dotnet server"
                dotnet build "${DOTNET_PROJECT}" -c Release -o "${DOTNET_OUT_DIR}" >/dev/null
                ;;
            cgdk10)
                log "build cgdk10 server"
                build_cgdk_server
                ;;
        esac
    done
}

start_server()
{
    local stack="$1"
    local host="$2"
    local port="$3"
    local size="$4"
    local server_log="$5"

    local -a cmd=()
    case "${stack}" in
        asio)
            cmd=("${ASIO_BIN}")
            ;;
        zlink)
            cmd=("${ZLINK_BIN}")
            ;;
        cppserver)
            cmd=("${CPPSERVER_BIN}")
            ;;
        dotnet)
            cmd=(dotnet "${DOTNET_DLL}")
            ;;
        cgdk10)
            cmd=("${CGDK_BIN}")
            ;;
        *)
            log "unknown stack: ${stack}"
            return 2
            ;;
    esac

    cmd+=(
        --host "${host}"
        --port "${port}"
        --size "${size}"
        --sndbuf "${SNDBUF}"
        --rcvbuf "${RCVBUF}"
        --backlog "${BACKLOG}"
        --tcp-nodelay "${TCP_NODELAY}"
        --io-threads "${SERVER_IO_THREADS}"
    )

    (
        exec "${cmd[@]}" >"${server_log}" 2>&1
    ) &
    ACTIVE_SERVER_PID="$!"
    ACTIVE_SERVER_STACK="${stack}"

    if ! wait_for_port "${host}" "${port}" "${SERVER_START_TIMEOUT}"; then
        return 1
    fi

    return 0
}

kv_from_file()
{
    local prefix="$1"
    local key="$2"
    local file="$3"
    awk -v p="${prefix}" -v k="${key}" '
      $1 == p {
        for (i = 2; i <= NF; ++i) {
          split($i, kv, "=");
          if (kv[1] == k) {
            print kv[2];
            exit;
          }
        }
      }
    ' "${file}"
}

value_or_zero()
{
    local v="$1"
    if [[ -z "${v}" ]]; then
        echo "0"
    else
        echo "${v}"
    fi
}

append_metric_row()
{
    local stack="$1"
    local size="$2"
    local repeat_idx="$3"
    local attempt="$4"
    local client_rc="$5"
    local throughput_msg_s="$6"
    local throughput_mib_s="$7"
    local p50_us="$8"
    local p95_us="$9"
    local p99_us="${10}"
    local pass_fail="${11}"
    local port="${12}"
    local client_log="${13}"
    local server_log="${14}"

    printf "%s\n" \
      "${stack},${size},${repeat_idx},${attempt},${CCU},${DURATION},${INFLIGHT},${client_rc},${throughput_msg_s},${throughput_mib_s},${p50_us},${p95_us},${p99_us},${pass_fail},${port},${client_log},${server_log}" \
      >>"${METRICS_CSV}"
}

run_case_once()
{
    local stack="$1"
    local size="$2"
    local repeat_idx="$3"
    local attempt="$4"
    local port=""

    local tag="${stack}_s${size}_r${repeat_idx}_a${attempt}"
    local server_log="${LOG_DIR}/${tag}_server.log"
    local client_log="${LOG_DIR}/${tag}_client.log"

    if ! allocate_free_port "${HOST}"; then
        log "port allocation failed stack=${stack} size=${size} repeat=${repeat_idx} attempt=${attempt}"
        append_metric_row "${stack}" "${size}" "${repeat_idx}" "${attempt}" "3" "0" "0" "0" "0" "0" "FAIL" "0" "${client_log}" "${server_log}"
        return 1
    fi
    port="${ALLOCATED_PORT}"

    log "run start stack=${stack} size=${size} repeat=${repeat_idx} attempt=${attempt} ccu=${CCU} duration=${DURATION} inflight=${INFLIGHT} port=${port}"

    if ! start_server "${stack}" "${HOST}" "${port}" "${size}" "${server_log}"; then
        log "server start failed stack=${stack} size=${size} repeat=${repeat_idx} attempt=${attempt}"
        append_metric_row "${stack}" "${size}" "${repeat_idx}" "${attempt}" "3" "0" "0" "0" "0" "0" "FAIL" "${port}" "${client_log}" "${server_log}"
        stop_active_server
        return 1
    fi

    set +e
    "${CLIENT_BIN}" \
        --host "${HOST}" \
        --port "${port}" \
        --ccu "${CCU}" \
        --size "${size}" \
        --duration "${DURATION}" \
        --inflight "${INFLIGHT}" \
        --io-threads "${CLIENT_IO_THREADS}" \
        >"${client_log}" 2>&1
    local client_rc="$?"
    set -e

    stop_active_server

    local throughput_msg_s throughput_mib_s p50_us p95_us p99_us pass_fail
    throughput_msg_s="$(value_or_zero "$(kv_from_file RESULT throughput_msg_s "${client_log}")")"
    throughput_mib_s="$(value_or_zero "$(kv_from_file RESULT throughput_mib_s "${client_log}")")"
    p50_us="$(value_or_zero "$(kv_from_file RESULT p50_us "${client_log}")")"
    p95_us="$(value_or_zero "$(kv_from_file RESULT p95_us "${client_log}")")"
    p99_us="$(value_or_zero "$(kv_from_file RESULT p99_us "${client_log}")")"
    pass_fail="$(kv_from_file RESULT pass_fail "${client_log}")"
    if [[ -z "${pass_fail}" ]]; then
        pass_fail="FAIL"
    fi

    append_metric_row "${stack}" "${size}" "${repeat_idx}" "${attempt}" "${client_rc}" "${throughput_msg_s}" "${throughput_mib_s}" "${p50_us}" "${p95_us}" "${p99_us}" "${pass_fail}" "${port}" "${client_log}" "${server_log}"

    if [[ "${client_rc}" == "0" && "${pass_fail}" == "PASS" ]]; then
        log "run pass stack=${stack} size=${size} repeat=${repeat_idx} tp=${throughput_msg_s} p95_us=${p95_us}"
        return 0
    fi

    log "run fail stack=${stack} size=${size} repeat=${repeat_idx} rc=${client_rc} pass=${pass_fail}"
    return 1
}

run_case_with_retry()
{
    local stack="$1"
    local size="$2"
    local repeat_idx="$3"
    local attempt

    for attempt in $(seq 1 "${MAX_ATTEMPTS_PER_CASE}"); do
        if run_case_once "${stack}" "${size}" "${repeat_idx}" "${attempt}"; then
            if (( CASE_GAP_SEC > 0 )); then
                sleep "${CASE_GAP_SEC}"
            fi
            return 0
        fi

        if (( attempt < MAX_ATTEMPTS_PER_CASE )); then
            log "retry stack=${stack} size=${size} repeat=${repeat_idx} next_attempt=$((attempt + 1)) delay=${RETRY_DELAY_SEC}s"
            if (( RETRY_DELAY_SEC > 0 )); then
                sleep "${RETRY_DELAY_SEC}"
            fi
        fi
    done

    FAILED_CASES=$((FAILED_CASES + 1))
    if (( CASE_GAP_SEC > 0 )); then
        sleep "${CASE_GAP_SEC}"
    fi
    return 1
}

generate_reports()
{
    python3 - "${METRICS_CSV}" "${SUMMARY_JSON}" "${COMPARISON_MD}" "${REPEATS}" "$(IFS=,; echo "${RUN_STACKS[*]}")" "$(IFS=,; echo "${RUN_SIZES[*]}")" <<'PY'
import csv
import json
import statistics
import sys
from collections import defaultdict

metrics_csv, summary_json, comparison_md, repeats_s, stacks_s, sizes_s = sys.argv[1:7]
repeats = int(repeats_s)
selected_stacks = [x for x in stacks_s.split(',') if x]
selected_sizes = [int(x) for x in sizes_s.split(',') if x]
expected_rows = len(selected_stacks) * len(selected_sizes) * repeats

rows = []
with open(metrics_csv, newline='') as f:
    reader = csv.DictReader(f)
    for row in reader:
        rows.append(row)

keyed = {}
for row in rows:
    key = (row.get('stack', ''), row.get('size', ''), row.get('repeat', ''))
    keyed[key] = row

dedup_rows = list(keyed.values())
actual_rows = len(dedup_rows)

count_summary = defaultdict(lambda: defaultdict(lambda: {'total': 0, 'pass': 0}))
values = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
invalid_rows = []

for row in dedup_rows:
    stack = row.get('stack', '')
    try:
        size = int(row.get('size', '0'))
    except Exception:
        invalid_rows.append(row)
        continue

    if stack not in selected_stacks or size not in selected_sizes:
        continue

    count_summary[size][stack]['total'] += 1

    if row.get('pass_fail') != 'PASS' or row.get('client_rc') != '0':
        continue

    def fnum(name):
        text = row.get(name, '')
        try:
            return float(text)
        except Exception:
            return None

    tp = fnum('throughput_msg_s')
    mib = fnum('throughput_mib_s')
    p50 = fnum('p50_us')
    p95 = fnum('p95_us')
    p99 = fnum('p99_us')

    if None in (tp, mib, p50, p95, p99):
        invalid_rows.append(row)
        continue

    count_summary[size][stack]['pass'] += 1
    values[size][stack]['throughput_msg_s'].append(tp)
    values[size][stack]['throughput_mib_s'].append(mib)
    values[size][stack]['p50_us'].append(p50)
    values[size][stack]['p95_us'].append(p95)
    values[size][stack]['p99_us'].append(p99)

medians = defaultdict(dict)
for size in selected_sizes:
    for stack in selected_stacks:
        if not values[size][stack]['throughput_msg_s']:
            continue
        medians[size][stack] = {
            'throughput_msg_s': statistics.median(values[size][stack]['throughput_msg_s']),
            'throughput_mib_s': statistics.median(values[size][stack]['throughput_mib_s']),
            'p50_us': statistics.median(values[size][stack]['p50_us']),
            'p95_us': statistics.median(values[size][stack]['p95_us']),
            'p99_us': statistics.median(values[size][stack]['p99_us']),
        }

summary = {
    'config': {
        'stacks': selected_stacks,
        'sizes': selected_sizes,
        'repeats': repeats,
    },
    'expected_rows': expected_rows,
    'actual_rows': actual_rows,
    'row_mismatch': actual_rows != expected_rows,
    'invalid_pass_rows': len(invalid_rows),
    'pass_count': {},
    'medians': {},
}

for size in selected_sizes:
    summary['pass_count'][str(size)] = {}
    summary['medians'][str(size)] = {}
    for stack in selected_stacks:
        summary['pass_count'][str(size)][stack] = count_summary[size][stack]
        if stack in medians[size]:
            summary['medians'][str(size)][stack] = medians[size][stack]

with open(summary_json, 'w') as f:
    json.dump(summary, f, indent=2)

lines = []
lines.append('# STREAM Echo Comparison')
lines.append('')
lines.append(f'- expected_rows: {expected_rows}')
lines.append(f'- actual_rows: {actual_rows}')
lines.append(f'- row_mismatch: {summary["row_mismatch"]}')
lines.append(f'- invalid_pass_rows: {len(invalid_rows)}')
lines.append('')
lines.append('## Throughput Median (PASS rows)')
lines.append('')
lines.append('| size | stack | msg/s | MiB/s |')
lines.append('|---:|---|---:|---:|')
for size in selected_sizes:
    for stack in selected_stacks:
        m = medians[size].get(stack)
        if not m:
            lines.append(f'| {size} | {stack} | n/a | n/a |')
            continue
        lines.append(f"| {size} | {stack} | {m['throughput_msg_s']:.2f} | {m['throughput_mib_s']:.2f} |")

lines.append('')
lines.append('## Latency Median (PASS rows)')
lines.append('')
lines.append('| size | stack | p50_us | p95_us | p99_us |')
lines.append('|---:|---|---:|---:|---:|')
for size in selected_sizes:
    for stack in selected_stacks:
        m = medians[size].get(stack)
        if not m:
            lines.append(f'| {size} | {stack} | n/a | n/a | n/a |')
            continue
        lines.append(f"| {size} | {stack} | {m['p50_us']:.2f} | {m['p95_us']:.2f} | {m['p99_us']:.2f} |")

lines.append('')
lines.append('## PASS/FAIL')
lines.append('')
lines.append('| size | stack | pass/total |')
lines.append('|---:|---|---:|')
for size in selected_sizes:
    for stack in selected_stacks:
        c = count_summary[size][stack]
        lines.append(f"| {size} | {stack} | {c['pass']}/{c['total']} |")

with open(comparison_md, 'w') as f:
    f.write('\n'.join(lines) + '\n')
PY
}

main()
{
    mkdir -p "${RESULT_DIR}" "${LOG_DIR}"
    : >"${SCENARIO_LOG}"

    log "run scope stacks=$(IFS=,; echo "${RUN_STACKS[*]}") sizes=$(IFS=,; echo "${RUN_SIZES[*]}")"
    log "settings host=${HOST} ccu=${CCU} duration=${DURATION} repeats=${REPEATS} inflight=${INFLIGHT} client_io_threads=${CLIENT_IO_THREADS} server_io_threads=${SERVER_IO_THREADS} max_attempts=${MAX_ATTEMPTS_PER_CASE} retry_delay=${RETRY_DELAY_SEC}s case_gap=${CASE_GAP_SEC}s server_start_timeout=${SERVER_START_TIMEOUT}s port_scan_limit=${PORT_SCAN_LIMIT} sndbuf=${SNDBUF} rcvbuf=${RCVBUF} backlog=${BACKLOG} tcp_nodelay=${TCP_NODELAY} base_port=${BASE_PORT}"
    log_kernel_limits

    cat >"${METRICS_CSV}" <<'CSV'
stack,size,repeat,attempt,ccu,duration,inflight,client_rc,throughput_msg_s,throughput_mib_s,p50_us,p95_us,p99_us,pass_fail,port,client_log,server_log
CSV

    build_selected

    local stack size repeat_idx
    for stack in "${RUN_STACKS[@]}"; do
        for size in "${RUN_SIZES[@]}"; do
            for repeat_idx in $(seq 1 "${REPEATS}"); do
                run_case_with_retry "${stack}" "${size}" "${repeat_idx}" || true
            done
        done
    done

    generate_reports

    log "result_dir=${RESULT_DIR}"
    log "metrics_csv=${METRICS_CSV}"
    log "summary_json=${SUMMARY_JSON}"
    log "comparison_md=${COMPARISON_MD}"

    if (( FAILED_CASES > 0 )); then
        log "completed with failures failed_cases=${FAILED_CASES}"
        return 1
    fi

    log "completed all runs"
    return 0
}

main "$@"
