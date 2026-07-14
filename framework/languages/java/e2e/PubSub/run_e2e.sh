#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/e2e-redis-common.sh"

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
REDIS_CONTAINER=""
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
SCENARIO="${1:-all}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/PubSub}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/PubSub-gradle-cache}"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=""
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:pubsub:${run_id}}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_SECONDS=5
if [[ "${LOCAL_READINESS_TIMEOUT_SECONDS}" != 3 \
   || "${LOCAL_READINESS_ATTEMPTS}" != 30 \
   || "${ROUTE_SETTLE_SECONDS:-}" != 5 ]]; then
  echo "PubSub must use 3s readiness and 5s route settle limits" >&2
  exit 1
fi

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for log in "${log_dir}"/*.log; do
    [[ -f "${log}" ]] || continue
    echo "===== ${log} =====" >&2
    tail -n 200 "${log}" >&2 || true
  done
}

descendants() {
  local pid="$1"
  local child
  (pgrep -P "${pid}" 2>/dev/null || true) | while read -r child; do
    descendants "${child}"
    echo "${child}"
  done
}

cleanup() {
  local status="$?"
  set +e
  print_logs "${status}"
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    local pid="${pids[$i]}"
    for child in $(descendants "${pid}"); do
      kill "${child}" >/dev/null 2>&1 || true
    done
    kill "${pid}" >/dev/null 2>&1 || true
  done
  sleep 0.5
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    local pid="${pids[$i]}"
    for child in $(descendants "${pid}"); do
      kill -9 "${child}" >/dev/null 2>&1 || true
    done
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
  if [[ -n "${REDIS_CONTAINER}" ]]; then
    docker rm -fv "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  fi
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

reserve_ports() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(8):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:3]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[3:]))
finally:
    for sock in sockets:
        sock.close()
PY
}

port_of() {
  echo "${1##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_health() {
  local name="$1"
  local endpoint="$2"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if python3 - "${endpoint}/health" <<'PY'
import sys
import urllib.request

try:
    with urllib.request.urlopen(sys.argv[1], timeout=0.1) as response:
        sys.exit(0 if 200 <= response.status < 300 else 1)
except Exception:
    sys.exit(1)
PY
    then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} health at ${endpoint}" >&2
  return 1
}

wait_marker() {
  local file="$1"
  for _ in $(seq 1 600); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for marker ${file}" >&2
  return 1
}

start_redis_container() {
  local redis_port
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required for ${SCENARIO}; it provisions a dedicated Redis location store." >&2
    exit 1
  fi
  zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
    "zlink-redis-java-e2e" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="127.0.0.1:${redis_port}"
  export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon --no-parallel --max-workers=1 "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/pub-sub-client/bin/pub-sub-client"
}

publisher_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Publisher/install/pub-sub-publisher/bin/pub-sub-publisher"
}

subscriber_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Subscriber/install/pub-sub-subscriber/bin/pub-sub-subscriber"
}

start_publisher() {
  local suffix="${1:-publisher}"
  ZLINK_JAVA_E2E_PUBLISHER_HTTP="${PUBLISHER_HTTP}" \
  ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT="${PUBLISHER_ENDPOINT}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(publisher_bin)" >"${log_dir}/${suffix}.stdout.log" 2>"${log_dir}/${suffix}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port publisher-fanout "${PUBLISHER_ENDPOINT}"
  wait_health "${suffix}" "${PUBLISHER_HTTP}"
}

start_subscriber() {
  local rid="$1"
  local topics="$2"
  local http="$3"
  local delay="${4:-}"
  ZLINK_JAVA_E2E_SUBSCRIBER_RID="${rid}" \
  ZLINK_JAVA_E2E_TOPICS="${topics}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_JAVA_E2E_HANDLER_DELAY_MS="${delay}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(subscriber_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_health "${rid}" "${http}"
}

stop_pid() {
  local pid="$1"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

run_client_mode() {
  local mode="$1"
  local suffix="$2"
  ZLINK_JAVA_E2E_CLIENT_MODE="${mode}" \
  ZLINK_JAVA_E2E_PUBLISHER_HTTP="${PUBLISHER_HTTP}" \
  ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT="${PUBLISHER_ENDPOINT}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_SUB1_HTTP="${SUB1_HTTP}" \
  ZLINK_JAVA_E2E_SUB2_HTTP="${SUB2_HTTP}" \
  ZLINK_JAVA_E2E_SUB3_HTTP="${SUB3_HTTP}" \
  ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(client_bin)" >"${log_dir}/client-${suffix}.stdout.log" 2>"${log_dir}/client-${suffix}.stderr.log"
  cat "${log_dir}/client-${suffix}.stdout.log"
}

read -r _UNUSED_PUB _UNUSED_ROUTER PUBLISHER_ENDPOINT _UNUSED_HTTP PUBLISHER_HTTP SUB1_HTTP SUB2_HTTP SUB3_HTTP <<<"$(reserve_ports)"

gradle_run installDist
start_redis_container

PUBLISHER_READY="${log_dir}/publisher-ready"
PRELATE_CONTINUE="${log_dir}/prelate-continue"
LATE_READY="${log_dir}/late-ready"
LATE_CONTINUE="${log_dir}/late-continue"

start_publisher publisher
PUBLISHER_PID="${LAST_PID}"

case "${SCENARIO}" in
  PS-A1|PS-A2|PS-C1)
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-A3)
    ZLINK_JAVA_E2E_CLIENT_MODE="${SCENARIO}" \
    ZLINK_JAVA_E2E_PUBLISHER_HTTP="${PUBLISHER_HTTP}" \
    ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT="${PUBLISHER_ENDPOINT}" \
    ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
    ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
    ZLINK_JAVA_E2E_SUB1_HTTP="${SUB1_HTTP}" \
    ZLINK_JAVA_E2E_SUB2_HTTP="${SUB2_HTTP}" \
    ZLINK_JAVA_E2E_SUB3_HTTP="${SUB3_HTTP}" \
    ZLINK_JAVA_E2E_PUBLISHER_READY_FILE="${PUBLISHER_READY}" \
    ZLINK_JAVA_E2E_PRELATE_CONTINUE_FILE="${PRELATE_CONTINUE}" \
    ZLINK_JAVA_E2E_LATE_READY_FILE="${LATE_READY}" \
    ZLINK_JAVA_E2E_LATE_CONTINUE_FILE="${LATE_CONTINUE}" \
    ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR}" \
    ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
      "$(client_bin)" >"${log_dir}/client-PS-A3.stdout.log" 2>"${log_dir}/client-PS-A3.stderr.log" &
    CLIENT_PID="$!"
    pids+=("${CLIENT_PID}")
    wait_marker "${PUBLISHER_READY}"
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    touch "${PRELATE_CONTINUE}"
    wait_marker "${LATE_READY}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    touch "${LATE_CONTINUE}"
    wait "${CLIENT_PID}"
    cat "${log_dir}/client-PS-A3.stdout.log"
    grep -q "scenario PS-A3 passed" "${log_dir}/client-PS-A3.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-A4)
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-B1)
    start_subscriber sub-1 alpha "${SUB1_HTTP}" 750
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-B2)
    stop_pid "${PUBLISHER_PID}"
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  all)
    ;;
  *)
    echo "Unknown PubSub scenario: ${SCENARIO}" >&2
    exit 1
    ;;
esac

ZLINK_JAVA_E2E_CLIENT_MODE=default \
ZLINK_JAVA_E2E_PUBLISHER_HTTP="${PUBLISHER_HTTP}" \
ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT="${PUBLISHER_ENDPOINT}" \
ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
ZLINK_JAVA_E2E_SUB1_HTTP="${SUB1_HTTP}" \
ZLINK_JAVA_E2E_SUB2_HTTP="${SUB2_HTTP}" \
ZLINK_JAVA_E2E_SUB3_HTTP="${SUB3_HTTP}" \
ZLINK_JAVA_E2E_PUBLISHER_READY_FILE="${PUBLISHER_READY}" \
ZLINK_JAVA_E2E_PRELATE_CONTINUE_FILE="${PRELATE_CONTINUE}" \
ZLINK_JAVA_E2E_LATE_READY_FILE="${LATE_READY}" \
ZLINK_JAVA_E2E_LATE_CONTINUE_FILE="${LATE_CONTINUE}" \
ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log" &
CLIENT_PID="$!"
pids+=("${CLIENT_PID}")

wait_marker "${PUBLISHER_READY}"
start_subscriber sub-1 alpha "${SUB1_HTTP}"
SUB1_PID="${LAST_PID}"
start_subscriber sub-2 beta "${SUB2_HTTP}"
SUB2_PID="${LAST_PID}"
sleep "${ROUTE_SETTLE_SECONDS}"
touch "${PRELATE_CONTINUE}"

wait_marker "${LATE_READY}"
start_subscriber sub-3 gamma "${SUB3_HTTP}"
SUB3_PID="${LAST_PID}"
sleep "${ROUTE_SETTLE_SECONDS}"
touch "${LATE_CONTINUE}"

wait "${CLIENT_PID}"
cat "${log_dir}/client.stdout.log"

stop_pid "${SUB1_PID}"
run_client_mode subscriber-restarted ps-a4

start_subscriber sub-1 alpha "${SUB1_HTTP}" 750
SUB1_PID="${LAST_PID}"
sleep "${ROUTE_SETTLE_SECONDS}"
run_client_mode slow-subscriber ps-b1

stop_pid "${PUBLISHER_PID}"
run_client_mode publisher-restarted ps-b2

python3 - "${SUB1_HTTP}/evidence" >"${log_dir}/sub-1-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
python3 - "${SUB2_HTTP}/evidence" >"${log_dir}/sub-2-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
python3 - "${SUB3_HTTP}/evidence" >"${log_dir}/sub-3-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY

grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "HANDLER_MISSING/DROP/MissingEventMsg" "${log_dir}/sub-2-evidence.json"
echo "pub-sub e2e result=passed"
