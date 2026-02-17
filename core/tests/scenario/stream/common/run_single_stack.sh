#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STREAM_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${STREAM_DIR}/../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

STACK=""
HOST="127.0.0.1"
PORT=39090
SIZE=1024
INFLIGHT=10
CCU=10000
DURATION=5
SNDBUF=1048576
RCVBUF=1048576
BACKLOG=32768
TCP_NODELAY=1
IO_THREADS=16
CONNECT_TIMEOUT=60
CONNECT_RETRIES=50
CONNECT_RETRY_DELAY_MS=100
SKIP_BUILD=0

CLIENT_BIN="${BUILD_DIR}/bin/test_scenario_stream_client"
ASIO_BIN="${BUILD_DIR}/bin/test_scenario_stream_asio"
ZLINK_BIN="${BUILD_DIR}/bin/test_scenario_stream_zlink"

CPPSERVER_SRC_DIR="${STREAM_DIR}/cppserver/upstream"
CPPSERVER_BUILD_DIR="${CPPSERVER_SRC_DIR}/build-stream"
CPPSERVER_BIN="${CPPSERVER_BUILD_DIR}/cppserver-performance-stream_fixed_server"
CPPSERVER_UPSTREAM_ENTRY="${CPPSERVER_SRC_DIR}/performance/stream_fixed_server.cpp"

DOTNET_PROJECT="${STREAM_DIR}/dotnet/StreamServer.csproj"
DOTNET_OUT_DIR="${STREAM_DIR}/dotnet/bin/Release/stream-bench"
DOTNET_DLL="${DOTNET_OUT_DIR}/StreamServer.dll"

ACTIVE_SERVER_PID=""

usage()
{
    cat <<'EOF'
Usage:
  run_single_stack.sh --stack <asio|cppserver|dotnet|zlink> [options]

Required:
  --stack                 stack to run

Main options:
  --size <64|1024|65536> payload size (default: 1024)
  --inflight <N>          inflight per connection (default: 10)

Other options:
  --host <ip>             host (default: 127.0.0.1)
  --port <port>           port (default: 39090)
  --ccu <N>               connections (default: 10000)
  --duration <sec>        duration seconds (default: 5)
  --sndbuf <bytes>        socket sndbuf (default: 1048576)
  --rcvbuf <bytes>        socket rcvbuf (default: 1048576)
  --backlog <N>           server backlog (default: 32768)
  --tcp-nodelay <0|1>     TCP_NODELAY (default: 1)
  --io-threads <N>        io thread count (default: 16)
  --connect-timeout <s>   client connect timeout (default: 60)
  --connect-retries <N>   client retries (default: 50)
  --connect-retry-delay-ms <ms>
                          client retry delay ms (default: 100)
  --skip-build            skip build
  -h, --help              show help
EOF
}

parse_args()
{
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --stack)
                STACK="${2:-}"
                shift 2
                ;;
            --host)
                HOST="${2:-}"
                shift 2
                ;;
            --port)
                PORT="${2:-}"
                shift 2
                ;;
            --size)
                SIZE="${2:-}"
                shift 2
                ;;
            --inflight)
                INFLIGHT="${2:-}"
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
            --sndbuf)
                SNDBUF="${2:-}"
                shift 2
                ;;
            --rcvbuf)
                RCVBUF="${2:-}"
                shift 2
                ;;
            --backlog)
                BACKLOG="${2:-}"
                shift 2
                ;;
            --tcp-nodelay)
                TCP_NODELAY="${2:-}"
                shift 2
                ;;
            --io-threads)
                IO_THREADS="${2:-}"
                shift 2
                ;;
            --connect-timeout)
                CONNECT_TIMEOUT="${2:-}"
                shift 2
                ;;
            --connect-retries)
                CONNECT_RETRIES="${2:-}"
                shift 2
                ;;
            --connect-retry-delay-ms)
                CONNECT_RETRY_DELAY_MS="${2:-}"
                shift 2
                ;;
            --skip-build)
                SKIP_BUILD=1
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                echo "unknown option: $1" >&2
                usage
                exit 2
                ;;
        esac
    done
}

kv_from_line()
{
    local line="$1"
    local key="$2"
    awk -v k="${key}" '
      {
        for (i = 1; i <= NF; ++i) {
          split($i, kv, "=");
          if (kv[1] == k) {
            print kv[2];
            exit;
          }
        }
      }
    ' <<<"${line}"
}

print_or_na()
{
    local value="$1"
    if [[ -z "${value}" ]]; then
        echo "n/a"
    else
        echo "${value}"
    fi
}

print_start_failure_summary()
{
    local result_dir="$1"
    local client_log="$2"
    local server_log="$3"
    local reason="$4"

    echo "========== STREAM Single Run =========="
    echo "stack            : ${STACK}"
    echo "size             : ${SIZE}"
    echo "inflight         : ${INFLIGHT}"
    echo "ccu              : ${CCU}"
    echo "duration_s       : ${DURATION}"
    echo "status           : FAIL (${reason})"
    echo ""
    echo "[paths]"
    echo "result_dir       : ${result_dir}"
    echo "client_log       : ${client_log}"
    echo "server_log       : ${server_log}"
    if [[ -s "${server_log}" ]]; then
        echo ""
        echo "[server_log_tail]"
        tail -n 10 "${server_log}" || true
    fi
    echo "======================================="
}

wait_for_port()
{
    local host="$1"
    local port="$2"
    local timeout_s="${3:-25}"
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

stop_server()
{
    if [[ -z "${ACTIVE_SERVER_PID}" ]]; then
        return
    fi

    if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
        kill "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        for _ in $(seq 1 50); do
            if ! kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
                break
            fi
            sleep 0.05
        done
        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill -INT "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        fi
        for _ in $(seq 1 50); do
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

build_stack()
{
    if [[ "${SKIP_BUILD}" == "1" ]]; then
        return
    fi

    echo "[build] core(client + selected server)"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null 2>&1
    cmake --build "${BUILD_DIR}" --target test_scenario_stream_client -j"$(nproc)" >/dev/null 2>&1

    case "${STACK}" in
        asio)
            cmake --build "${BUILD_DIR}" --target test_scenario_stream_asio -j"$(nproc)" >/dev/null 2>&1
            ;;
        zlink)
            cmake --build "${BUILD_DIR}" --target test_scenario_stream_zlink -j"$(nproc)" >/dev/null 2>&1
            ;;
        cppserver)
            cat >"${CPPSERVER_UPSTREAM_ENTRY}" <<'CPP'
#include "../../test_scenario_stream_cppserver.cpp"
CPP
            cmake -S "${CPPSERVER_SRC_DIR}" -B "${CPPSERVER_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
            cmake --build "${CPPSERVER_BUILD_DIR}" --target cppserver-performance-stream_fixed_server -j"$(nproc)" >/dev/null 2>&1
            ;;
        dotnet)
            dotnet build "${DOTNET_PROJECT}" -c Release -o "${DOTNET_OUT_DIR}" >/dev/null 2>&1
            ;;
        *)
            echo "unsupported stack: ${STACK}" >&2
            exit 2
            ;;
    esac
}

start_server()
{
    local scenario_id="$1"
    local server_log="$2"
    local -a cmd=()

    case "${STACK}" in
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
            echo "unsupported stack: ${STACK}" >&2
            exit 2
            ;;
    esac

    cmd+=(
        --mode echo
        --host "${HOST}"
        --port "${PORT}"
        --size "${SIZE}"
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
    wait_for_port "${HOST}" "${PORT}" 25
}

main()
{
    parse_args "$@"

    if [[ -z "${STACK}" ]]; then
        echo "--stack is required" >&2
        usage
        exit 2
    fi

    local ts
    ts="$(date +%Y%m%d_%H%M%S)"
    local result_dir="${STREAM_DIR}/result/single_${ts}_${STACK}_s${SIZE}_i${INFLIGHT}"
    local server_log="${result_dir}/server.log"
    local client_log="${result_dir}/client.log"
    local scenario_id="single_${STACK}_s${SIZE}_i${INFLIGHT}_${ts}"

    mkdir -p "${result_dir}"

    trap stop_server EXIT

    build_stack

    if ! start_server "${scenario_id}" "${server_log}"; then
        stop_server
        echo "[warn] server start timeout stack=${STACK} port=${PORT}; retry once" >&2
        sleep 0.2
        if ! start_server "${scenario_id}" "${server_log}"; then
            stop_server
            print_start_failure_summary "${result_dir}" "${client_log}" "${server_log}" "server_start_timeout"
            exit 1
        fi
    fi

    set +e
    "${CLIENT_BIN}" \
        --mode echo \
        --host "${HOST}" \
        --port "${PORT}" \
        --ccu "${CCU}" \
        --size "${SIZE}" \
        --duration "${DURATION}" \
        --inflight "${INFLIGHT}" \
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

    stop_server

    local result_line
    result_line="$(grep -m1 '^RESULT ' "${client_log}" || true)"
    local pass_fail
    pass_fail="$(awk '/^RESULT /{for (i=1;i<=NF;++i){if($i ~ /^pass_fail=/){split($i,kv,"="); print kv[2]; exit}}}' "${client_log}")"

    local throughput_msg_s throughput_mib_s p50_us p95_us p99_us
    local connect_success connect_fail connect_ms parse_error timeout_error
    local incomplete_ratio
    throughput_msg_s="$(kv_from_line "${result_line}" "throughput_msg_s")"
    throughput_mib_s="$(kv_from_line "${result_line}" "throughput_mib_s")"
    p50_us="$(kv_from_line "${result_line}" "p50_us")"
    p95_us="$(kv_from_line "${result_line}" "p95_us")"
    p99_us="$(kv_from_line "${result_line}" "p99_us")"
    connect_success="$(kv_from_line "${result_line}" "connect_success")"
    connect_fail="$(kv_from_line "${result_line}" "connect_fail")"
    connect_ms="$(kv_from_line "${result_line}" "connect_ms")"
    parse_error="$(kv_from_line "${result_line}" "parse_error")"
    timeout_error="$(kv_from_line "${result_line}" "timeout_error")"
    incomplete_ratio="$(kv_from_line "${result_line}" "incomplete_ratio")"

    local server_metric_line
    server_metric_line="$(grep -m1 '^METRIC .*recv_msgs=' "${server_log}" || true)"
    local server_recv_msgs server_parse_error server_protocol_error server_send_error server_connections
    server_recv_msgs="$(kv_from_line "${server_metric_line}" "recv_msgs")"
    server_parse_error="$(kv_from_line "${server_metric_line}" "parse_error")"
    server_protocol_error="$(kv_from_line "${server_metric_line}" "protocol_error")"
    server_send_error="$(kv_from_line "${server_metric_line}" "send_error")"
    server_connections="$(kv_from_line "${server_metric_line}" "connections")"

    echo "========== STREAM Single Run =========="
    echo "stack            : ${STACK}"
    echo "size             : ${SIZE}"
    echo "inflight         : ${INFLIGHT}"
    echo "ccu              : ${CCU}"
    echo "duration_s       : ${DURATION}"
    echo "status           : $(print_or_na "${pass_fail}") (client_rc=${client_rc})"
    echo ""
    echo "[throughput]"
    echo "msg_per_sec      : $(print_or_na "${throughput_msg_s}")"
    echo "mib_per_sec      : $(print_or_na "${throughput_mib_s}")"
    echo ""
    echo "[latency_us]"
    echo "p50              : $(print_or_na "${p50_us}")"
    echo "p95              : $(print_or_na "${p95_us}")"
    echo "p99              : $(print_or_na "${p99_us}")"
    echo ""
    echo "[client]"
    echo "connect_success  : $(print_or_na "${connect_success}")"
    echo "connect_fail     : $(print_or_na "${connect_fail}")"
    echo "connect_ms       : $(print_or_na "${connect_ms}")"
    echo "parse_error      : $(print_or_na "${parse_error}")"
    echo "timeout_error    : $(print_or_na "${timeout_error}")"
    echo "incomplete_ratio : $(print_or_na "${incomplete_ratio}")"
    echo ""
    echo "[server]"
    echo "recv_msgs        : $(print_or_na "${server_recv_msgs}")"
    echo "parse_error      : $(print_or_na "${server_parse_error}")"
    echo "protocol_error   : $(print_or_na "${server_protocol_error}")"
    echo "send_error       : $(print_or_na "${server_send_error}")"
    echo "connections      : $(print_or_na "${server_connections}")"
    if [[ -z "${server_metric_line}" ]]; then
        echo "note             : server metric line not emitted"
    fi

    if grep -q '^METRIC .*connect_events=' "${server_log}" 2>/dev/null; then
        local zline
        zline="$(grep -m1 '^METRIC .*connect_events=' "${server_log}" || true)"
        echo "connect_events   : $(print_or_na "$(kv_from_line "${zline}" "connect_events")")"
        echo "disconnect_events: $(print_or_na "$(kv_from_line "${zline}" "disconnect_events")")"
    fi

    echo ""
    echo "[paths]"
    echo "result_dir       : ${result_dir}"
    echo "client_log       : ${client_log}"
    echo "server_log       : ${server_log}"
    echo "======================================="

    if [[ "${client_rc}" == "0" && "${pass_fail}" == "PASS" ]]; then
        exit 0
    fi

    exit 1
}

main "$@"
