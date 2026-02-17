#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

LOCK_FILE="/tmp/run_stream_compare.lock"
LOCK_FD=9

exec {LOCK_FD}>"${LOCK_FILE}"
if ! flock -n "${LOCK_FD}"; then
    echo "[$(date +'%F %T')] another run_stream_compare.sh is already running. Run only one benchmark at a time." >&2
    exit 1
fi

usage()
{
    cat <<'EOF'
Usage:
  run_stream_compare.sh [--stack <asio|cppserver|dotnet|zlink|all>] [--size <64|1024|65536|all>] [--inflight <N>]

Description:
  Run STREAM echo comparison matrix.
  Default (no options):
    4 stacks (asio, cppserver, dotnet, zlink)
    x 3 sizes (64, 1024, 65536)
    x repeats

  Targeted mode:
    --stack and/or --size filters matrix scope
    --inflight fixes inflight for selected size scope

Configure via environment variables (examples):
  CCU=10000 DURATION=5 REPEATS=3 ./run_stream_compare.sh
  CONNECT_TIMEOUT=60 CONNECT_RETRIES=50 IO_THREADS=16 ./run_stream_compare.sh

Targeted examples:
  ./run_stream_compare.sh --stack asio --size 1024 --inflight 20
  REPEATS=1 CCU=1000 ./run_stream_compare.sh --stack cppserver --size 64 --inflight 40

Main environment variables:
  HOST                    default: 127.0.0.1
  CCU                     default: 10000
  DURATION                default: 5
  REPEATS                 default: 3
  BASE_PORT               default: 39000
  SMOKE_CCU               default: 100
  SMOKE_DURATION          default: 2
  BASELINE_DURATION       default: 3
  SNDBUF                  default: 1048576
  RCVBUF                  default: 1048576
  BACKLOG                 default: 32768
  TCP_NODELAY             default: 1
  IO_THREADS              default: 8
  CONNECT_TIMEOUT         default: 20
  CONNECT_RETRIES         default: 4
  CONNECT_RETRY_DELAY_MS  default: 100
  RESULT_DIR              default: ./result/<timestamp>

Notes:
  - In targeted mode, smoke/baseline sweep are skipped.
  - Without --inflight in targeted mode, default inflight is used by size:
      64->10, 1024->10, 65536->1
  - For per-stack single runs, use:
      ./asio/run_single.sh
      ./cppserver/run_single.sh
      ./dotnet/run_single.sh
      ./zlink/run_single.sh
EOF
}

parse_args()
{
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
            --inflight)
                TARGET_INFLIGHT="${2:-}"
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
}

TARGET_STACK="all"
TARGET_SIZE="all"
TARGET_INFLIGHT=""

parse_args "$@"

RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/result/$(date +%Y%m%d_%H%M%S)}"
LOG_DIR="${RESULT_DIR}/logs"
SCENARIO_LOG="${RESULT_DIR}/scenario.log"
METRICS_CSV="${RESULT_DIR}/metrics.csv"
SUMMARY_JSON="${RESULT_DIR}/summary.json"
SWEEP_SUMMARY_CSV="${RESULT_DIR}/sweep_summary.csv"
SELECTED_INFLIGHT_JSON="${RESULT_DIR}/selected_inflight.json"
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

HOST="${HOST:-127.0.0.1}"
CCU="${CCU:-10000}"
DURATION="${DURATION:-5}"
REPEATS="${REPEATS:-3}"
BASE_PORT="${BASE_PORT:-39000}"

SMOKE_CCU="${SMOKE_CCU:-100}"
SMOKE_DURATION="${SMOKE_DURATION:-2}"
BASELINE_DURATION="${BASELINE_DURATION:-3}"

SNDBUF="${SNDBUF:-1048576}"
RCVBUF="${RCVBUF:-1048576}"
BACKLOG="${BACKLOG:-32768}"
TCP_NODELAY="${TCP_NODELAY:-1}"
IO_THREADS="${IO_THREADS:-8}"

CONNECT_TIMEOUT="${CONNECT_TIMEOUT:-20}"
CONNECT_RETRIES="${CONNECT_RETRIES:-4}"
CONNECT_RETRY_DELAY_MS="${CONNECT_RETRY_DELAY_MS:-100}"

SIZES=(64 1024 65536)
STACKS=(asio cppserver dotnet zlink)
RUN_SIZES=()
RUN_STACKS=()
TARGETED_MODE=0

validate_target_options()
{
    case "${TARGET_STACK}" in
        all|asio|cppserver|dotnet|zlink)
            ;;
        *)
            echo "invalid --stack: ${TARGET_STACK}" >&2
            exit 2
            ;;
    esac

    case "${TARGET_SIZE}" in
        all|64|1024|65536)
            ;;
        *)
            echo "invalid --size: ${TARGET_SIZE}" >&2
            exit 2
            ;;
    esac

    if [[ -n "${TARGET_INFLIGHT}" ]]; then
        if ! [[ "${TARGET_INFLIGHT}" =~ ^[0-9]+$ ]] || (( TARGET_INFLIGHT < 1 )); then
            echo "invalid --inflight: ${TARGET_INFLIGHT}" >&2
            exit 2
        fi
    fi
}

prepare_run_scope()
{
    validate_target_options

    RUN_STACKS=("${STACKS[@]}")
    RUN_SIZES=("${SIZES[@]}")

    if [[ "${TARGET_STACK}" != "all" ]]; then
        RUN_STACKS=("${TARGET_STACK}")
    fi
    if [[ "${TARGET_SIZE}" != "all" ]]; then
        RUN_SIZES=("${TARGET_SIZE}")
    fi

    if [[ "${TARGET_STACK}" != "all" || "${TARGET_SIZE}" != "all" || -n "${TARGET_INFLIGHT}" ]]; then
        TARGETED_MODE=1
    fi
}

write_selected_inflight_json()
{
    cat >"${SELECTED_INFLIGHT_JSON}" <<JSON
{
  "64": ${SELECTED_INFLIGHT[64]},
  "1024": ${SELECTED_INFLIGHT[1024]},
  "65536": ${SELECTED_INFLIGHT[65536]}
}
JSON
}

apply_target_inflight_override()
{
    if [[ -z "${TARGET_INFLIGHT}" ]]; then
        return
    fi

    local size
    for size in "${RUN_SIZES[@]}"; do
        SELECTED_INFLIGHT["${size}"]="${TARGET_INFLIGHT}"
    done
}

prepare_run_scope

declare -A SELECTED_INFLIGHT
SELECTED_INFLIGHT[64]=10
SELECTED_INFLIGHT[1024]=10
SELECTED_INFLIGHT[65536]=1

PORT_CURSOR="${BASE_PORT}"
ACTIVE_SERVER_PID=""

log()
{
    mkdir -p "${RESULT_DIR}" "${LOG_DIR}"
    echo "[$(date +'%F %T')] $*" | tee -a "${SCENARIO_LOG}"
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

num_or_default()
{
    local value="$1"
    local fallback="$2"
    if [[ -z "${value}" ]]; then
        echo "${fallback}"
    else
        echo "${value}"
    fi
}

float_le()
{
    local lhs="$1"
    local rhs="$2"
    awk -v l="${lhs}" -v r="${rhs}" 'BEGIN { exit ((l + 0.0) <= (r + 0.0) ? 0 : 1) }'
}

float_gt()
{
    local lhs="$1"
    local rhs="$2"
    awk -v l="${lhs}" -v r="${rhs}" 'BEGIN { exit ((l + 0.0) > (r + 0.0) ? 0 : 1) }'
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

stop_active_server()
{
    if [[ -z "${ACTIVE_SERVER_PID}" ]]; then
        return
    fi

    if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
        kill -INT "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        for _ in $(seq 1 40); do
            if ! kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
                break
            fi
            sleep 0.05
        done
        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        fi
        for _ in $(seq 1 60); do
            if ! kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
                break
            fi
            sleep 0.05
        done
        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill -9 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        fi
    fi

    wait "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
    ACTIVE_SERVER_PID=""
}

cleanup_on_exit()
{
    stop_active_server
}

trap cleanup_on_exit EXIT

start_server()
{
    local stack="$1"
    local host="$2"
    local port="$3"
    local size="$4"
    local scenario_id="$5"
    local server_log="$6"

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
        *)
            log "unknown stack: ${stack}"
            return 2
            ;;
    esac

    cmd+=(
        --mode echo
        --host "${host}"
        --port "${port}"
        --size "${size}"
        --scenario-id "${scenario_id}"
        --sndbuf "${SNDBUF}"
        --rcvbuf "${RCVBUF}"
        --backlog "${BACKLOG}"
        --tcp-nodelay "${TCP_NODELAY}"
        --io-threads "${IO_THREADS}"
    )

    (
        exec "${cmd[@]}" >"${server_log}" 2>&1
    ) &
    ACTIVE_SERVER_PID="$!"

    if ! wait_for_port "${host}" "${port}" 25; then
        log "server start timeout: stack=${stack} port=${port}"
        stop_active_server
        return 3
    fi

    return 0
}

run_case_once()
{
    local phase="$1"
    local stack="$2"
    local size="$3"
    local inflight="$4"
    local ccu="$5"
    local duration="$6"
    local repeat_idx="$7"
    local attempt_idx="${8:-1}"

    local port="${PORT_CURSOR}"
    PORT_CURSOR="$((PORT_CURSOR + 1))"

    local tag="${phase}_${stack}_s${size}_r${repeat_idx}_i${inflight}_a${attempt_idx}"
    local server_log="${LOG_DIR}/${tag}_server.log"
    local client_log="${LOG_DIR}/${tag}_client.log"
    local scenario_id="${tag}"

    log "run start phase=${phase} stack=${stack} size=${size} inflight=${inflight} ccu=${ccu} duration=${duration} port=${port}"

    if ! start_server "${stack}" "${HOST}" "${port}" "${size}" "${scenario_id}" "${server_log}"; then
        printf "%s\n" "${phase},${stack},${size},${repeat_idx},${ccu},${duration},${inflight},0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,FAIL,${port},${client_log},${server_log}" >>"${METRICS_CSV}"
        return 1
    fi

    set +e
    "${CLIENT_BIN}" \
        --mode echo \
        --host "${HOST}" \
        --port "${port}" \
        --ccu "${ccu}" \
        --size "${size}" \
        --duration "${duration}" \
        --inflight "${inflight}" \
        --scenario-id "${scenario_id}" \
        --sndbuf "${SNDBUF}" \
        --rcvbuf "${RCVBUF}" \
        --tcp-nodelay "${TCP_NODELAY}" \
        --io-threads "${IO_THREADS}" \
        --connect-timeout "${CONNECT_TIMEOUT}" \
        --connect-retries "${CONNECT_RETRIES}" \
        --connect-retry-delay-ms "${CONNECT_RETRY_DELAY_MS}" \
        >"${client_log}" 2>&1
    local client_rc="$?"
    set -e

    stop_active_server

    local throughput_msg_s throughput_mib_s p50_us p95_us p99_us
    local connect_success connect_fail connect_ms
    local client_parse_error client_timeout_error incomplete_ratio client_line_pass
    local server_parse_error server_protocol_error server_send_error

    throughput_msg_s="$(num_or_default "$(kv_from_file RESULT throughput_msg_s "${client_log}")" "0")"
    throughput_mib_s="$(num_or_default "$(kv_from_file RESULT throughput_mib_s "${client_log}")" "0")"
    p50_us="$(num_or_default "$(kv_from_file RESULT p50_us "${client_log}")" "0")"
    p95_us="$(num_or_default "$(kv_from_file RESULT p95_us "${client_log}")" "0")"
    p99_us="$(num_or_default "$(kv_from_file RESULT p99_us "${client_log}")" "0")"
    connect_success="$(num_or_default "$(kv_from_file RESULT connect_success "${client_log}")" "0")"
    connect_fail="$(num_or_default "$(kv_from_file RESULT connect_fail "${client_log}")" "0")"
    connect_ms="$(num_or_default "$(kv_from_file RESULT connect_ms "${client_log}")" "0")"
    client_parse_error="$(num_or_default "$(kv_from_file RESULT parse_error "${client_log}")" "0")"
    client_timeout_error="$(num_or_default "$(kv_from_file RESULT timeout_error "${client_log}")" "0")"
    incomplete_ratio="$(num_or_default "$(kv_from_file RESULT incomplete_ratio "${client_log}")" "1")"
    client_line_pass="$(num_or_default "$(kv_from_file RESULT pass_fail "${client_log}")" "FAIL")"

    server_parse_error="$(num_or_default "$(kv_from_file METRIC parse_error "${server_log}")" "0")"
    server_protocol_error="$(num_or_default "$(kv_from_file METRIC protocol_error "${server_log}")" "0")"
    server_send_error="$(num_or_default "$(kv_from_file METRIC send_error "${server_log}")" "0")"

    local combined_pass="PASS"

    if [[ "${connect_fail}" != "0" ]]; then
        combined_pass="FAIL"
    fi
    if [[ "${client_timeout_error}" != "0" ]]; then
        combined_pass="FAIL"
    fi
    if [[ "${client_parse_error}" != "0" ]]; then
        combined_pass="FAIL"
    fi
    if [[ "${server_parse_error}" != "0" ]]; then
        combined_pass="FAIL"
    fi
    if [[ "${server_protocol_error}" != "0" ]]; then
        combined_pass="FAIL"
    fi
    if [[ "${server_send_error}" != "0" ]]; then
        combined_pass="FAIL"
    fi
    if ! float_le "${incomplete_ratio}" "0.01"; then
        combined_pass="FAIL"
    fi
    if ! float_gt "${p95_us}" "0"; then
        combined_pass="FAIL"
    fi
    if [[ "${client_line_pass}" != "PASS" ]]; then
        combined_pass="FAIL"
    fi
    if [[ "${client_rc}" != "0" ]]; then
        combined_pass="FAIL"
    fi

    printf "%s\n" \
      "${phase},${stack},${size},${repeat_idx},${ccu},${duration},${inflight},${throughput_msg_s},${throughput_mib_s},${p50_us},${p95_us},${p99_us},${connect_success},${connect_fail},${connect_ms},${client_parse_error},${client_timeout_error},${server_parse_error},${server_protocol_error},${server_send_error},${incomplete_ratio},${client_rc},${combined_pass},${port},${client_log},${server_log}" \
      >>"${METRICS_CSV}"

    if [[ "${combined_pass}" == "PASS" ]]; then
        log "run pass phase=${phase} stack=${stack} size=${size} inflight=${inflight} throughput_msg_s=${throughput_msg_s} p95_us=${p95_us}"
        return 0
    fi

    log "run fail phase=${phase} stack=${stack} size=${size} inflight=${inflight} rc=${client_rc} client_pass=${client_line_pass} connect_fail=${connect_fail} timeout=${client_timeout_error} client_parse=${client_parse_error} server_parse=${server_parse_error} server_proto=${server_protocol_error} server_send=${server_send_error} incomplete=${incomplete_ratio}"
    return 1
}

run_case_with_retry()
{
    local phase="$1"
    local stack="$2"
    local size="$3"
    local inflight="$4"
    local ccu="$5"
    local duration="$6"
    local repeat_idx="$7"

    if run_case_once "${phase}" "${stack}" "${size}" "${inflight}" "${ccu}" "${duration}" "${repeat_idx}" 1; then
        return 0
    fi

    log "retry once phase=${phase} stack=${stack} size=${size} inflight=${inflight}"
    run_case_once "${phase}" "${stack}" "${size}" "${inflight}" "${ccu}" "${duration}" "${repeat_idx}" 2
}

build_all()
{
    log "build C++ scenario targets"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null
    cmake --build "${BUILD_DIR}" \
        --target test_scenario_stream_client test_scenario_stream_asio test_scenario_stream_zlink \
        -j"$(nproc)" >/dev/null

    log "build cppserver stream_fixed_server"
    cat >"${CPPSERVER_UPSTREAM_ENTRY}" <<'CPP'
#include "../../test_scenario_stream_cppserver.cpp"
CPP
    cmake -S "${CPPSERVER_SRC_DIR}" -B "${CPPSERVER_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "${CPPSERVER_BUILD_DIR}" --target cppserver-performance-stream_fixed_server -j"$(nproc)" >/dev/null

    log "build dotnet stream server"
    dotnet build "${DOTNET_PROJECT}" -c Release -o "${DOTNET_OUT_DIR}" >/dev/null

    [[ -x "${CLIENT_BIN}" ]]
    [[ -x "${ASIO_BIN}" ]]
    [[ -x "${ZLINK_BIN}" ]]
    [[ -x "${CPPSERVER_BIN}" ]]
    [[ -f "${DOTNET_DLL}" ]]
}

run_smoke()
{
    log "smoke run start"
    local failed=0

    for stack in "${RUN_STACKS[@]}"; do
        if ! run_case_with_retry "smoke" "${stack}" 1024 10 "${SMOKE_CCU}" "${SMOKE_DURATION}" 1; then
            failed=1
        fi
    done

    if [[ "${failed}" != "0" ]]; then
        log "smoke failed"
        return 1
    fi

    log "smoke passed"
    return 0
}

choose_baseline_inflight()
{
    log "baseline sweep start (cppserver only)"

    cat >"${SWEEP_SUMMARY_CSV}" <<'CSV'
size,candidate_inflight,throughput_msg_s,pass_fail,client_rc,connect_fail,parse_error,timeout_error,incomplete_ratio,p95_us
CSV

    for size in "${SIZES[@]}"; do
        local candidates=()
        case "${size}" in
            64)
                candidates=(10 20 40)
                ;;
            1024)
                candidates=(10 12 16 20)
                ;;
            65536)
                candidates=(1 2 4)
                ;;
        esac

        local best_inflight="${SELECTED_INFLIGHT[${size}]}"
        local best_tp="0"
        local found=0

        for inflight in "${candidates[@]}"; do
            run_case_once "baseline" "cppserver" "${size}" "${inflight}" "${CCU}" "${BASELINE_DURATION}" 1 || true

            local row
            row="$(tail -n 1 "${METRICS_CSV}")"
            local pass_fail tp client_rc connect_fail parse_error timeout_error incomplete_ratio p95_us

            pass_fail="$(echo "${row}" | awk -F',' '{print $23}')"
            tp="$(echo "${row}" | awk -F',' '{print $8}')"
            client_rc="$(echo "${row}" | awk -F',' '{print $22}')"
            connect_fail="$(echo "${row}" | awk -F',' '{print $14}')"
            parse_error="$(echo "${row}" | awk -F',' '{print $16}')"
            timeout_error="$(echo "${row}" | awk -F',' '{print $17}')"
            incomplete_ratio="$(echo "${row}" | awk -F',' '{print $21}')"
            p95_us="$(echo "${row}" | awk -F',' '{print $11}')"

            printf "%s\n" "${size},${inflight},${tp},${pass_fail},${client_rc},${connect_fail},${parse_error},${timeout_error},${incomplete_ratio},${p95_us}" >>"${SWEEP_SUMMARY_CSV}"

            if [[ "${pass_fail}" != "PASS" ]]; then
                continue
            fi

            if [[ "${found}" == "0" ]]; then
                best_inflight="${inflight}"
                best_tp="${tp}"
                found=1
                continue
            fi

            if float_gt "${tp}" "${best_tp}"; then
                best_tp="${tp}"
                best_inflight="${inflight}"
            elif awk -v a="${tp}" -v b="${best_tp}" 'BEGIN {exit (a == b ? 0 : 1)}'; then
                if (( inflight < best_inflight )); then
                    best_inflight="${inflight}"
                fi
            fi
        done

        SELECTED_INFLIGHT["${size}"]="${best_inflight}"
        log "baseline selected size=${size} inflight=${best_inflight}"
    done

    write_selected_inflight_json
}

run_main_matrix()
{
    log "main matrix start"

    local failed=0
    for repeat_idx in $(seq 1 "${REPEATS}"); do
        for size in "${RUN_SIZES[@]}"; do
            local inflight="${SELECTED_INFLIGHT[${size}]}"
            for stack in "${RUN_STACKS[@]}"; do
                if ! run_case_with_retry "main" "${stack}" "${size}" "${inflight}" "${CCU}" "${DURATION}" "${repeat_idx}"; then
                    failed=1
                fi
            done
        done
    done

    if [[ "${failed}" != "0" ]]; then
        log "main matrix has FAIL cases"
        return 1
    fi

    log "main matrix all PASS"
    return 0
}

generate_reports()
{
    python3 - "${METRICS_CSV}" "${SUMMARY_JSON}" "${COMPARISON_MD}" <<'PY'
import csv
import json
import math
import statistics
import sys
from collections import defaultdict

metrics_csv, summary_json, comparison_md = sys.argv[1:4]

rows = []
with open(metrics_csv, newline='') as f:
    reader = csv.DictReader(f)
    for row in reader:
        rows.append(row)

main_rows = [r for r in rows if r.get('phase') == 'main']
pass_rows = [r for r in main_rows if r.get('pass_fail') == 'PASS']

sizes = sorted({int(r['size']) for r in main_rows})
stacks = sorted({r['stack'] for r in main_rows})

agg = defaultdict(lambda: defaultdict(dict))
count_summary = defaultdict(lambda: defaultdict(lambda: {'total': 0, 'pass': 0}))

fields_num = [
    'throughput_msg_s', 'throughput_mib_s', 'p50_us', 'p95_us', 'p99_us'
]

for r in main_rows:
    size = int(r['size'])
    stack = r['stack']
    count_summary[size][stack]['total'] += 1
    if r['pass_fail'] == 'PASS':
        count_summary[size][stack]['pass'] += 1

for size in sizes:
    for stack in stacks:
        target = [r for r in pass_rows if int(r['size']) == size and r['stack'] == stack]
        if not target:
            continue
        med = {}
        for f in fields_num:
            vals = [float(r[f]) for r in target]
            med[f] = statistics.median(vals)
        agg[size][stack] = med

summary = {
    'sizes': sizes,
    'stacks': stacks,
    'pass_count': {},
    'medians': {},
    'best_by_size': {}
}

for size in sizes:
    summary['pass_count'][str(size)] = {}
    summary['medians'][str(size)] = {}

    for stack in stacks:
        c = count_summary[size][stack]
        summary['pass_count'][str(size)][stack] = c
        if stack in agg[size]:
            summary['medians'][str(size)][stack] = agg[size][stack]

    best_stack = None
    best_tp = -1.0
    for stack in stacks:
        if stack not in agg[size]:
            continue
        tp = agg[size][stack]['throughput_msg_s']
        if tp > best_tp:
            best_tp = tp
            best_stack = stack

    if best_stack:
        summary['best_by_size'][str(size)] = {
            'stack': best_stack,
            'throughput_msg_s': best_tp
        }

with open(summary_json, 'w') as f:
    json.dump(summary, f, indent=2)

lines = []
lines.append('# STREAM Echo Comparison')
lines.append('')
lines.append('## Throughput Median (PASS runs only)')
lines.append('')
lines.append('| size | stack | msg/s | MiB/s | vs best | vs cppserver |')
lines.append('|---:|---|---:|---:|---:|---:|')

for size in sizes:
    best_tp = max([agg[size][s]['throughput_msg_s'] for s in agg[size]], default=0.0)
    cpp_tp = agg[size].get('cppserver', {}).get('throughput_msg_s', 0.0)
    for stack in stacks:
        if stack not in agg[size]:
            lines.append(f'| {size} | {stack} | n/a | n/a | n/a | n/a |')
            continue
        tp = agg[size][stack]['throughput_msg_s']
        mib = agg[size][stack]['throughput_mib_s']
        vs_best = (tp / best_tp * 100.0) if best_tp > 0 else 0.0
        vs_cpp = (tp / cpp_tp * 100.0) if cpp_tp > 0 else 0.0
        lines.append(f'| {size} | {stack} | {tp:.2f} | {mib:.2f} | {vs_best:.1f}% | {vs_cpp:.1f}% |')

lines.append('')
lines.append('## Latency Median (PASS runs only)')
lines.append('')
lines.append('| size | stack | p50_us | p95_us | p99_us |')
lines.append('|---:|---|---:|---:|---:|')
for size in sizes:
    for stack in stacks:
        if stack not in agg[size]:
            lines.append(f'| {size} | {stack} | n/a | n/a | n/a |')
            continue
        m = agg[size][stack]
        lines.append(f"| {size} | {stack} | {m['p50_us']:.2f} | {m['p95_us']:.2f} | {m['p99_us']:.2f} |")

lines.append('')
lines.append('## PASS/FAIL')
lines.append('')
lines.append('| size | stack | pass/total |')
lines.append('|---:|---|---:|')
for size in sizes:
    for stack in stacks:
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

    if [[ -r /proc/sys/net/core/somaxconn ]]; then
        log "system somaxconn=$(cat /proc/sys/net/core/somaxconn)"
    fi
    if [[ -r /proc/sys/net/ipv4/tcp_max_syn_backlog ]]; then
        log "system tcp_max_syn_backlog=$(cat /proc/sys/net/ipv4/tcp_max_syn_backlog)"
    fi

    cat >"${METRICS_CSV}" <<'CSV'
phase,stack,size,repeat,ccu,duration,inflight,throughput_msg_s,throughput_mib_s,p50_us,p95_us,p99_us,connect_success,connect_fail,connect_ms,client_parse_error,client_timeout_error,server_parse_error,server_protocol_error,server_send_error,incomplete_ratio,client_rc,pass_fail,port,client_log,server_log
CSV

    build_all

    if [[ "${TARGETED_MODE}" == "1" ]]; then
        apply_target_inflight_override
        write_selected_inflight_json
        log "targeted mode stack=${TARGET_STACK} size=${TARGET_SIZE} inflight=${TARGET_INFLIGHT:-auto-default}"
        log "targeted run stacks=$(IFS=,; echo "${RUN_STACKS[*]}") sizes=$(IFS=,; echo "${RUN_SIZES[*]}")"
    else
        run_smoke
        choose_baseline_inflight
    fi

    run_main_matrix

    generate_reports

    log "completed"
    log "result_dir=${RESULT_DIR}"
    log "metrics_csv=${METRICS_CSV}"
    log "summary_json=${SUMMARY_JSON}"
    log "comparison_md=${COMPARISON_MD}"
}

main "$@"
