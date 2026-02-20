#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

LOCK_FILE="/tmp/bench_streamcompare.lock"
LOCK_FD=9
exec {LOCK_FD}>"${LOCK_FILE}"
if ! flock -n "${LOCK_FD}"; then
    echo "[$(date +'%F %T')] another stream benchmark is running" >&2
    exit 1
fi

usage()
{
    cat <<'USAGE'
Usage:
  run_benchmarks.sh [options]

Options:
  --stack <asio|cppserver|dotnet|zlink|zmq|cgdk10|netty|all|csv>
  --size <64|1024|65536|all|csv>
  --ccu <N>                    default: 10000
  --runs <N>                   default: 1
  --warmup <sec>               default: 2
  --duration <sec>             default: 10
  --client-io-threads <N>      default: 4
  --server-io-threads <N>      default: 4
  --server-start-timeout <sec> default: 40
  --stack-gap <sec>            default: 5
  -h, --help

Examples:
  ./run_benchmarks.sh
  ./run_benchmarks.sh --stack zlink,zmq --size 1024 --runs 3
USAGE
}

STACKS_ALL=(asio cppserver dotnet zlink zmq cgdk10 netty)
SIZES_ALL=(64 1024 65536)

TARGET_STACK="all"
TARGET_SIZE="all"
CCU=10000
RUNS=1
WARMUP=2
DURATION=10
CLIENT_IO_THREADS=4
SERVER_IO_THREADS=4
SERVER_START_TIMEOUT=40
STACK_GAP_SEC=5

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
        --runs)
            RUNS="${2:-}"
            shift 2
            ;;
        --warmup)
            WARMUP="${2:-}"
            shift 2
            ;;
        --duration)
            DURATION="${2:-}"
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
        --server-start-timeout)
            SERVER_START_TIMEOUT="${2:-}"
            shift 2
            ;;
        --stack-gap)
            STACK_GAP_SEC="${2:-}"
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

validate_int()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value < 1 )); then
        echo "invalid ${name}: ${value}" >&2
        exit 2
    fi
}

validate_int "--ccu" "${CCU}"
validate_int "--runs" "${RUNS}"
validate_int "--warmup" "${WARMUP}"
validate_int "--duration" "${DURATION}"
validate_int "--client-io-threads" "${CLIENT_IO_THREADS}"
validate_int "--server-io-threads" "${SERVER_IO_THREADS}"
validate_int "--server-start-timeout" "${SERVER_START_TIMEOUT}"
validate_int "--stack-gap" "${STACK_GAP_SEC}"

RUN_STACKS=()
if [[ "${TARGET_STACK}" == "all" ]]; then
    RUN_STACKS=("${STACKS_ALL[@]}")
else
    IFS=',' read -r -a RUN_STACKS <<<"${TARGET_STACK}"
fi

RUN_SIZES=()
if [[ "${TARGET_SIZE}" == "all" ]]; then
    RUN_SIZES=("${SIZES_ALL[@]}")
else
    IFS=',' read -r -a RUN_SIZES <<<"${TARGET_SIZE}"
fi

if [[ ${#RUN_STACKS[@]} -eq 0 ]]; then
    echo "empty stack set" >&2
    exit 2
fi
if [[ ${#RUN_SIZES[@]} -eq 0 ]]; then
    echo "empty size set" >&2
    exit 2
fi

for s in "${RUN_STACKS[@]}"; do
    case "${s}" in
        asio|cppserver|dotnet|zlink|zmq|cgdk10|netty)
            ;;
        *)
            echo "invalid stack: ${s}" >&2
            exit 2
            ;;
    esac
done

for sz in "${RUN_SIZES[@]}"; do
    case "${sz}" in
        64|1024|65536)
            ;;
        *)
            echo "invalid size: ${sz}" >&2
            exit 2
            ;;
    esac
done

MAX_RUN_SIZE=0
for sz in "${RUN_SIZES[@]}"; do
    if (( sz > MAX_RUN_SIZE )); then
        MAX_RUN_SIZE="${sz}"
    fi
done

RESULT_DIR="${RESULT_DIR:-${SCRIPT_DIR}/results/$(date +%Y%m%d_%H%M%S)}"
LOG_DIR="${RESULT_DIR}/logs"
SCENARIO_LOG="${RESULT_DIR}/benchmark.log"
METRICS_CSV="${RESULT_DIR}/metrics.csv"
SUMMARY_JSON="${RESULT_DIR}/summary.json"
REPORT_MD="${RESULT_DIR}/comparison.md"
SKIP_CSV="${RESULT_DIR}/skipped_stacks.csv"

CLIENT_BIN="${BUILD_DIR}/bin/bench_streamcompare_client"
ASIO_BIN="${BUILD_DIR}/bin/test_scenario_stream_asio"
ZLINK_BIN="${BUILD_DIR}/bin/test_scenario_stream_zlink"
ZMQ_BIN="${BUILD_DIR}/bin/test_scenario_stream_zmq"
ZMQ_LIB_DIR="${ROOT_DIR}/core/tests/scenario/stream/zmq/libzmq_dist/linux-x64/lib"

CPPSERVER_SRC_DIR="${ROOT_DIR}/core/tests/scenario/stream/cppserver/upstream"
CPPSERVER_BUILD_DIR="${CPPSERVER_SRC_DIR}/build-stream"
CPPSERVER_BIN="${CPPSERVER_BUILD_DIR}/cppserver-performance-stream_fixed_server"
CPPSERVER_UPSTREAM_ENTRY="${CPPSERVER_SRC_DIR}/performance/stream_fixed_server.cpp"

DOTNET_PROJECT="${ROOT_DIR}/core/tests/scenario/stream/dotnet/StreamServer.csproj"
DOTNET_OUT_DIR="${ROOT_DIR}/core/tests/scenario/stream/dotnet/bin/Release/stream-bench"
DOTNET_DLL="${DOTNET_OUT_DIR}/StreamServer.dll"

NETTY_PROJECT_DIR="${ROOT_DIR}/core/tests/scenario/stream/netty"
NETTY_BUILD_DIR="${NETTY_PROJECT_DIR}/build"
NETTY_BIN="${NETTY_BUILD_DIR}/install/netty-stream-server/bin/netty-stream-server"
NETTY_JAVA_HOME="${NETTY_JAVA_HOME:-}"
NETTY_JAVA_BIN=""
NETTY_JAVA_VERSION=""
NETTY_GRADLE_BIN="${NETTY_GRADLE_BIN:-}"
NETTY_GRADLE_VERSION=""
NETTY_GRADLE_MIN_VERSION="8.8"
NETTY_GRADLE_FALLBACK_VERSION="8.10.2"
NETTY_GRADLE_TOOLS_DIR="${NETTY_PROJECT_DIR}/.gradle-tools"
NETTY_GRADLE_FALLBACK_DIR="${NETTY_GRADLE_TOOLS_DIR}/gradle-${NETTY_GRADLE_FALLBACK_VERSION}"

CGDK_REPO_URL="${CGDK_REPO_URL:-https://github.com/CGLabs/CGDK10.Cpp.git}"
CGDK_ROOT_DIR="${ROOT_DIR}/core/tests/scenario/stream/cgdk10"
CGDK_SRC_DIR="${CGDK_ROOT_DIR}/upstream/CGDK10.Cpp"
CGDK_BUILD_DIR="${CGDK_ROOT_DIR}/build"
CGDK_BIN="${CGDK_BUILD_DIR}/test_scenario_stream_cgdk10"
CGDK_SERVER_SRC="${CGDK_ROOT_DIR}/test_scenario_stream_cgdk10.cpp"
CGDK_LIB_DIR="${CGDK_SRC_DIR}/lib/cgdk/sdk10/ubuntu"

ACTIVE_SERVER_PID=""
ACTIVE_SERVER_STACK=""
HOST="${HOST:-127.0.0.1}"
PORT_CURSOR="${BASE_PORT:-22000}"
PORT_SCAN_LIMIT=5000
ALLOCATED_PORT=""

ACTIVE_STACKS=()
FAILED_CASES=0

log()
{
    mkdir -p "${RESULT_DIR}" "${LOG_DIR}"
    echo "[$(date +'%F %T')] $*" | tee -a "${SCENARIO_LOG}"
}

java_major_version()
{
    local java_bin="$1"
    local major=""
    major="$("${java_bin}" -version 2>&1 | awk -F'[\".]' '/version/ { if ($2 == "1") print $3; else print $2; exit }')"
    if ! [[ "${major}" =~ ^[0-9]+$ ]]; then
        return 1
    fi
    printf '%s' "${major}"
}

version_ge()
{
    local have="$1"
    local need="$2"
    local top=""
    top="$(printf '%s\n%s\n' "${need}" "${have}" | sort -V | tail -n1)"
    [[ "${top}" == "${have}" ]]
}

gradle_version()
{
    local gradle_bin="$1"
    local version=""
    version="$("${gradle_bin}" -v 2>/dev/null | awk '/Gradle [0-9]/{print $2; exit}')"
    if ! [[ "${version}" =~ ^[0-9]+(\.[0-9]+){1,2}$ ]]; then
        return 1
    fi
    printf '%s' "${version}"
}

resolve_netty_gradle()
{
    if [[ -n "${NETTY_GRADLE_BIN}" ]]; then
        if [[ ! -x "${NETTY_GRADLE_BIN}" ]]; then
            log "NETTY_GRADLE_BIN is not executable: ${NETTY_GRADLE_BIN}"
            return 1
        fi
        NETTY_GRADLE_VERSION="$(gradle_version "${NETTY_GRADLE_BIN}")" || {
            log "failed to detect version from NETTY_GRADLE_BIN=${NETTY_GRADLE_BIN}"
            return 1
        }
        if ! version_ge "${NETTY_GRADLE_VERSION}" "${NETTY_GRADLE_MIN_VERSION}"; then
            log "NETTY_GRADLE_BIN version=${NETTY_GRADLE_VERSION} is too old (need >=${NETTY_GRADLE_MIN_VERSION})"
            return 1
        fi
        return 0
    fi

    local system_gradle=""
    local system_gradle_version=""
    system_gradle="$(command -v gradle || true)"
    if [[ -n "${system_gradle}" ]]; then
        system_gradle_version="$(gradle_version "${system_gradle}")" || {
            log "failed to detect version from system gradle=${system_gradle}"
            return 1
        }
        if version_ge "${system_gradle_version}" "${NETTY_GRADLE_MIN_VERSION}"; then
            NETTY_GRADLE_BIN="${system_gradle}"
            NETTY_GRADLE_VERSION="${system_gradle_version}"
            return 0
        fi
    fi

    if ! command -v curl >/dev/null 2>&1; then
        log "curl is required to download Gradle ${NETTY_GRADLE_FALLBACK_VERSION} for netty stack"
        return 1
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        log "unzip is required to install Gradle ${NETTY_GRADLE_FALLBACK_VERSION} for netty stack"
        return 1
    fi

    mkdir -p "${NETTY_GRADLE_TOOLS_DIR}"
    if [[ ! -x "${NETTY_GRADLE_FALLBACK_DIR}/bin/gradle" ]]; then
        local dist_zip="${NETTY_GRADLE_TOOLS_DIR}/gradle-${NETTY_GRADLE_FALLBACK_VERSION}-bin.zip"
        local dist_url="https://services.gradle.org/distributions/gradle-${NETTY_GRADLE_FALLBACK_VERSION}-bin.zip"
        if [[ ! -f "${dist_zip}" ]]; then
            log "download gradle version=${NETTY_GRADLE_FALLBACK_VERSION}"
            curl -fsSL -o "${dist_zip}" "${dist_url}"
        fi
        unzip -q -o "${dist_zip}" -d "${NETTY_GRADLE_TOOLS_DIR}"
    fi

    NETTY_GRADLE_BIN="${NETTY_GRADLE_FALLBACK_DIR}/bin/gradle"
    if [[ ! -x "${NETTY_GRADLE_BIN}" ]]; then
        log "fallback gradle not found: ${NETTY_GRADLE_BIN}"
        return 1
    fi

    NETTY_GRADLE_VERSION="$(gradle_version "${NETTY_GRADLE_BIN}")" || {
        log "failed to detect version from fallback gradle=${NETTY_GRADLE_BIN}"
        return 1
    }
    return 0
}

resolve_netty_java()
{
    if [[ -n "${NETTY_JAVA_BIN}" ]]; then
        return 0
    fi

    local candidate=""
    local candidate_home=""
    local major=""

    if [[ -n "${NETTY_JAVA_HOME}" ]]; then
        candidate="${NETTY_JAVA_HOME}/bin/java"
        if [[ ! -x "${candidate}" ]]; then
            log "netty java not found at NETTY_JAVA_HOME=${NETTY_JAVA_HOME}"
            return 1
        fi
        major="$(java_major_version "${candidate}")" || {
            log "failed to detect java version from ${candidate}"
            return 1
        }
        if (( major < 22 )); then
            log "NETTY_JAVA_HOME java major=${major} is too old (need >=22)"
            return 1
        fi
        NETTY_JAVA_BIN="${candidate}"
        NETTY_JAVA_VERSION="${major}"
        return 0
    fi

    if [[ -n "${JAVA_HOME:-}" ]]; then
        candidate="${JAVA_HOME}/bin/java"
        if [[ -x "${candidate}" ]]; then
            major="$(java_major_version "${candidate}")" || {
                log "failed to detect java version from JAVA_HOME=${JAVA_HOME}"
                return 1
            }
            if (( major >= 22 )); then
                NETTY_JAVA_HOME="${JAVA_HOME}"
                NETTY_JAVA_BIN="${candidate}"
                NETTY_JAVA_VERSION="${major}"
                return 0
            fi
        fi
    fi

    candidate="$(command -v java || true)"
    if [[ -z "${candidate}" ]]; then
        log "java not found in PATH (need JDK 22+ for netty stack)"
        return 1
    fi
    candidate="$(readlink -f "${candidate}" || printf '%s' "${candidate}")"

    major="$(java_major_version "${candidate}")" || {
        log "failed to detect java version from PATH java=${candidate}"
        return 1
    }
    if (( major < 22 )); then
        log "PATH java major=${major} is too old for netty (need >=22)"
        return 1
    fi

    candidate_home="$(cd "$(dirname "${candidate}")/.." && pwd)"
    NETTY_JAVA_HOME="${candidate_home}"
    NETTY_JAVA_BIN="${candidate}"
    NETTY_JAVA_VERSION="${major}"
    return 0
}

wait_for_port()
{
    local host="$1"
    local port="$2"
    local timeout_s="$3"
    local start_ts
    start_ts="$(date +%s)"

    while true; do
        if python3 - "${host}" "${port}" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(0.2)
try:
    rc = sock.connect_ex((host, port))
finally:
    sock.close()

sys.exit(0 if rc == 0 else 1)
PY
        then
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
        else
            kill -INT "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
        fi
        sleep 0.5

        if kill -0 "${ACTIVE_SERVER_PID}" >/dev/null 2>&1; then
            kill "${ACTIVE_SERVER_PID}" >/dev/null 2>&1 || true
            sleep 0.5
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

record_skip()
{
    local stack="$1"
    local reason="$2"
    printf "%s,%s\n" "${stack}" "${reason}" >>"${SKIP_CSV}"
    log "skip stack=${stack} reason=${reason}"
}

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

build_core_targets()
{
    log "configure core build"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
        -DZLINK_BUILD_TESTS=ON \
        -DBUILD_BENCHMARKS=ON >/dev/null

    log "build bench_streamcompare_client"
    cmake --build "${BUILD_DIR}" --target bench_streamcompare_client -j"$(nproc)" >/dev/null
}

try_build_stack()
{
    local stack="$1"

    case "${stack}" in
        asio)
            cmake --build "${BUILD_DIR}" --target test_scenario_stream_asio -j"$(nproc)" >/dev/null
            ;;
        zlink)
            cmake --build "${BUILD_DIR}" --target test_scenario_stream_zlink -j"$(nproc)" >/dev/null
            ;;
        zmq)
            cmake --build "${BUILD_DIR}" --target test_scenario_stream_zmq -j"$(nproc)" >/dev/null
            ;;
        cppserver)
            cat >"${CPPSERVER_UPSTREAM_ENTRY}" <<'CPP'
#include "../../test_scenario_stream_cppserver.cpp"
CPP
            cmake -S "${CPPSERVER_SRC_DIR}" -B "${CPPSERVER_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null
            cmake --build "${CPPSERVER_BUILD_DIR}" --target cppserver-performance-stream_fixed_server -j"$(nproc)" >/dev/null
            ;;
        dotnet)
            dotnet build "${DOTNET_PROJECT}" -c Release -o "${DOTNET_OUT_DIR}" >/dev/null
            ;;
        netty)
            resolve_netty_java || return 1
            resolve_netty_gradle || return 1
            log "netty using java major=${NETTY_JAVA_VERSION} home=${NETTY_JAVA_HOME} gradle=${NETTY_GRADLE_VERSION}"
            JAVA_HOME="${NETTY_JAVA_HOME}" PATH="${NETTY_JAVA_HOME}/bin:${PATH}" \
                "${NETTY_GRADLE_BIN}" -p "${NETTY_PROJECT_DIR}" --no-daemon installDist >/dev/null
            [[ -x "${NETTY_BIN}" ]]
            ;;
        cgdk10)
            build_cgdk_server
            ;;
        *)
            return 1
            ;;
    esac
}

build_selected()
{
    build_core_targets

    local stack
    for stack in "${RUN_STACKS[@]}"; do
        log "build stack=${stack}"
        set +e
        try_build_stack "${stack}"
        local rc="$?"
        set -e
        if [[ "${rc}" == "0" ]]; then
            ACTIVE_STACKS+=("${stack}")
        else
            record_skip "${stack}" "build_failed"
        fi
    done
}

start_server()
{
    local stack="$1"
    local host="$2"
    local port="$3"
    local server_log="$4"
    local size_hint="$5"

    local -a cmd=()
    case "${stack}" in
        asio)
            cmd=("${ASIO_BIN}")
            ;;
        zlink)
            cmd=("${ZLINK_BIN}")
            ;;
        zmq)
            cmd=("${ZMQ_BIN}")
            ;;
        cppserver)
            cmd=("${CPPSERVER_BIN}")
            ;;
        dotnet)
            cmd=(dotnet "${DOTNET_DLL}")
            ;;
        netty)
            cmd=("${NETTY_BIN}")
            ;;
        cgdk10)
            cmd=("${CGDK_BIN}")
            ;;
        *)
            return 2
            ;;
    esac

    cmd+=(
        --host "${host}"
        --port "${port}"
        --size "${size_hint}"
        --sndbuf "1048576"
        --rcvbuf "1048576"
        --backlog "32768"
        --tcp-nodelay "1"
        --io-threads "${SERVER_IO_THREADS}"
        --raw-echo "1"
    )

    if [[ "${stack}" == "zmq" ]]; then
        (
            export LD_LIBRARY_PATH="${ZMQ_LIB_DIR}:${LD_LIBRARY_PATH:-}"
            exec "${cmd[@]}" >"${server_log}" 2>&1
        ) &
    elif [[ "${stack}" == "netty" ]]; then
        (
            export JAVA_HOME="${NETTY_JAVA_HOME}"
            export PATH="${NETTY_JAVA_HOME}/bin:${PATH}"
            exec "${cmd[@]}" >"${server_log}" 2>&1
        ) &
    elif [[ "${stack}" == "cgdk10" ]]; then
        (
            export LD_LIBRARY_PATH="${CGDK_LIB_DIR}:${LD_LIBRARY_PATH:-}"
            exec "${cmd[@]}" >"${server_log}" 2>&1
        ) &
    else
        (
            exec "${cmd[@]}" >"${server_log}" 2>&1
        ) &
    fi

    ACTIVE_SERVER_PID="$!"
    ACTIVE_SERVER_STACK="${stack}"

    if ! wait_for_port "${host}" "${port}" "${SERVER_START_TIMEOUT}"; then
        return 1
    fi

    return 0
}

append_rows_from_client_log()
{
    local stack="$1"
    local client_rc="$2"
    local client_log="$3"
    local server_log="$4"

    python3 - "${METRICS_CSV}" "${stack}" "${CCU}" "${client_rc}" \
        "${client_log}" "${server_log}" <<'PY'
import csv
import sys

metrics_csv, stack, ccu, client_rc, client_log, server_log = sys.argv[1:7]
ccu = int(ccu)

rows = []

with open(client_log, encoding="utf-8", errors="replace") as f:
    for raw_line in f:
        line = raw_line.strip()
        if not line.startswith("RESULT "):
            continue
        fields = {}
        for token in line.split()[1:]:
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            fields[key] = value

        row = {
            "stack": stack,
            "size": fields.get("size", "0"),
            "run": fields.get("run", "0"),
            "ccu": str(ccu),
            "inflight": "1",
            "client_rc": str(client_rc),
            "throughput_bps": fields.get("throughput_bps", "0"),
            "throughput_mib_s": fields.get("throughput_mib_s", "0"),
            "p50_us": fields.get("p50_us", "0"),
            "p95_us": fields.get("p95_us", "0"),
            "p99_us": fields.get("p99_us", "0"),
            "connect_ok": fields.get("connect_ok", "0"),
            "connect_fail": fields.get("connect_fail", "0"),
            "send_err": fields.get("send_err", "0"),
            "recv_err": fields.get("recv_err", "0"),
            "timeout": fields.get("timeout", "0"),
            "pass_fail": fields.get("pass_fail", "FAIL"),
            "client_log": client_log,
            "server_log": server_log,
        }
        rows.append(row)

with open(metrics_csv, "a", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(
        f,
        fieldnames=[
            "stack", "size", "run", "ccu", "inflight", "client_rc",
            "throughput_bps", "throughput_mib_s", "p50_us", "p95_us",
            "p99_us", "connect_ok", "connect_fail", "send_err",
            "recv_err", "timeout", "pass_fail", "client_log", "server_log",
        ],
    )
    for row in rows:
        writer.writerow(row)

print(len(rows))
PY
}

run_stack_once()
{
    local stack="$1"
    local port="$2"
    local server_log="$3"
    local client_log="$4"

    local sizes_csv
    sizes_csv="$(IFS=,; echo "${RUN_SIZES[*]}")"

    set +e
    "${CLIENT_BIN}" \
        --host "${HOST}" \
        --port "${port}" \
        --ccu "${CCU}" \
        --sizes "${sizes_csv}" \
        --runs "${RUNS}" \
        --warmup "${WARMUP}" \
        --duration "${DURATION}" \
        --inflight "1" \
        --latency-sample-rate "0" \
        --io-threads "${CLIENT_IO_THREADS}" \
        >"${client_log}" 2>&1
    local client_rc="$?"
    set -e

    local row_count
    row_count="$(append_rows_from_client_log "${stack}" "${client_rc}" "${client_log}" "${server_log}")"

    if [[ "${client_rc}" != "0" ]]; then
        log "client failed stack=${stack} rc=${client_rc}"
        FAILED_CASES="$((FAILED_CASES + 1))"
        return 1
    fi

    if [[ "${row_count}" == "0" ]]; then
        log "no RESULT rows parsed stack=${stack}"
        FAILED_CASES="$((FAILED_CASES + 1))"
        return 1
    fi

    return 0
}

main()
{
    mkdir -p "${RESULT_DIR}" "${LOG_DIR}"
    : >"${SCENARIO_LOG}"

    cat >"${METRICS_CSV}" <<'CSV'
stack,size,run,ccu,inflight,client_rc,throughput_bps,throughput_mib_s,p50_us,p95_us,p99_us,connect_ok,connect_fail,send_err,recv_err,timeout,pass_fail,client_log,server_log
CSV

    cat >"${SKIP_CSV}" <<'CSV'
stack,reason
CSV

    log "scope stacks=$(IFS=,; echo "${RUN_STACKS[*]}") sizes=$(IFS=,; echo "${RUN_SIZES[*]}")"
    log "settings ccu=${CCU} runs=${RUNS} warmup=${WARMUP}s duration=${DURATION}s inflight=1 server_size=${MAX_RUN_SIZE}"

    build_selected

    if [[ ${#ACTIVE_STACKS[@]} -eq 0 ]]; then
        log "no active stacks after build"
        return 1
    fi

    local stack
    for stack in "${ACTIVE_STACKS[@]}"; do
        if ! allocate_free_port "${HOST}"; then
            record_skip "${stack}" "port_allocation_failed"
            FAILED_CASES="$((FAILED_CASES + 1))"
            continue
        fi

        local port="${ALLOCATED_PORT}"
        local server_log="${LOG_DIR}/${stack}_server.log"
        local client_log="${LOG_DIR}/${stack}_client.log"

        log "start stack=${stack} port=${port}"
        if ! start_server "${stack}" "${HOST}" "${port}" "${server_log}" "${MAX_RUN_SIZE}"; then
            record_skip "${stack}" "server_start_failed"
            stop_active_server
            FAILED_CASES="$((FAILED_CASES + 1))"
            continue
        fi

        run_stack_once "${stack}" "${port}" "${server_log}" "${client_log}" || true

        stop_active_server

        if (( STACK_GAP_SEC > 0 )); then
            sleep "${STACK_GAP_SEC}"
        fi
    done

    python3 "${SCRIPT_DIR}/run_comparison.py" \
        --metrics "${METRICS_CSV}" \
        --summary "${SUMMARY_JSON}" \
        --report "${REPORT_MD}" \
        --runs "${RUNS}" \
        --stacks "$(IFS=,; echo "${ACTIVE_STACKS[*]}")" \
        --sizes "$(IFS=,; echo "${RUN_SIZES[*]}")" \
        --skip-file "${SKIP_CSV}"

    log "result_dir=${RESULT_DIR}"
    log "metrics_csv=${METRICS_CSV}"
    log "summary_json=${SUMMARY_JSON}"
    log "comparison_md=${REPORT_MD}"

    if (( FAILED_CASES > 0 )); then
        log "completed with failures failed_cases=${FAILED_CASES}"
        return 1
    fi

    log "completed"
    return 0
}

main "$@"
