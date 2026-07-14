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
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/RuntimeMonitoring}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/RuntimeMonitoring-gradle-cache}"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=""
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:runtime-monitoring:${run_id}}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_SECONDS=5
if [[ "${LOCAL_READINESS_TIMEOUT_SECONDS}" != 3 \
   || "${LOCAL_READINESS_ATTEMPTS}" != 30 \
   || "${ROUTE_SETTLE_SECONDS}" != 5 ]]; then
  echo "RuntimeMonitoring must use 3s readiness and 5s route settle limits" >&2
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
    for _ in range(13):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(str(port) for port in ports))
finally:
    for sock in sockets:
        sock.close()
PY
}

tcp() {
  echo "tcp://127.0.0.1:$1"
}

http() {
  echo "http://127.0.0.1:$1"
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
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/runtime-monitoring-client/bin/runtime-monitoring-client"
}

service_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Service/install/runtime-monitoring-service/bin/runtime-monitoring-service"
}

filtered_service_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-FilteredService/install/runtime-monitoring-filtered-service/bin/runtime-monitoring-filtered-service"
}

throwing_service_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-ThrowingService/install/runtime-monitoring-throwing-service/bin/runtime-monitoring-throwing-service"
}

trigger_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Trigger/install/runtime-monitoring-trigger/bin/runtime-monitoring-trigger"
}

read -r API_PORT HANDSHAKE_PORT SPOT_PORT SPOT_PUB_PORT SVC_HTTP_PORT FILTER_API_PORT FILTER_HTTP_PORT THROW_API_PORT THROW_HTTP_PORT TRIGGER_HTTP_PORT _ _ _ <<<"$(reserve_ports)"
API_ENDPOINT="$(tcp "${API_PORT}")"
HANDSHAKE_ENDPOINT="$(tcp "${HANDSHAKE_PORT}")"
SPOT_ENDPOINT="$(tcp "${SPOT_PORT}")"
SPOT_PUB_ENDPOINT="$(tcp "${SPOT_PUB_PORT}")"
SERVICE_HTTP="$(http "${SVC_HTTP_PORT}")"
FILTER_API_ENDPOINT="$(tcp "${FILTER_API_PORT}")"
FILTER_HTTP="$(http "${FILTER_HTTP_PORT}")"
THROW_API_ENDPOINT="$(tcp "${THROW_API_PORT}")"
THROW_HTTP="$(http "${THROW_HTTP_PORT}")"
TRIGGER_HTTP="$(http "${TRIGGER_HTTP_PORT}")"

gradle_run installDist
start_redis_container

ZLINK_JAVA_E2E_RID="svc-a" \
ZLINK_JAVA_E2E_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_JAVA_E2E_HANDSHAKE_ENDPOINT="${HANDSHAKE_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT="${SPOT_PUB_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${SERVICE_HTTP}" \
ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(service_bin)" >"${log_dir}/service.stdout.log" 2>"${log_dir}/service.stderr.log" &
pids+=("$!")
wait_port service-api "${API_ENDPOINT}"
wait_port service-http "${SERVICE_HTTP}"

ZLINK_JAVA_E2E_RID="svc-b" \
ZLINK_JAVA_E2E_API_ENDPOINT="${FILTER_API_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${FILTER_HTTP}" \
ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
ZLINK_JAVA_E2E_ENABLE_HANDSHAKE="false" \
ZLINK_JAVA_E2E_ENABLE_SPOT="false" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(filtered_service_bin)" >"${log_dir}/filtered-service.stdout.log" 2>"${log_dir}/filtered-service.stderr.log" &
pids+=("$!")
wait_port filtered-service-api "${FILTER_API_ENDPOINT}"
wait_port filtered-service-http "${FILTER_HTTP}"

ZLINK_JAVA_E2E_RID="svc-throw" \
ZLINK_JAVA_E2E_API_ENDPOINT="${THROW_API_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${THROW_HTTP}" \
ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
ZLINK_JAVA_E2E_ENABLE_HANDSHAKE="false" \
ZLINK_JAVA_E2E_ENABLE_SPOT="false" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(throwing_service_bin)" >"${log_dir}/throwing-service.stdout.log" 2>"${log_dir}/throwing-service.stderr.log" &
pids+=("$!")
wait_port throwing-service-api "${THROW_API_ENDPOINT}"
wait_port throwing-service-http "${THROW_HTTP}"

ZLINK_JAVA_E2E_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_JAVA_E2E_SERVICE_B_API_ENDPOINT="${FILTER_API_ENDPOINT}" \
ZLINK_JAVA_E2E_HANDSHAKE_ENDPOINT="${HANDSHAKE_ENDPOINT}" \
ZLINK_JAVA_E2E_SERVICE_HTTP="${SERVICE_HTTP}" \
ZLINK_JAVA_E2E_SERVICE_B_HTTP="${FILTER_HTTP}" \
ZLINK_JAVA_E2E_TRIGGER_HTTP="${TRIGGER_HTTP}" \
ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
ZLINK_JAVA_E2E_FILTERED_SERVICE_BIN="$(filtered_service_bin)" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(trigger_bin)" >"${log_dir}/trigger.stdout.log" 2>"${log_dir}/trigger.stderr.log" &
pids+=("$!")
wait_port trigger-http "${TRIGGER_HTTP}"
sleep "${ROUTE_SETTLE_SECONDS}"

ZLINK_JAVA_E2E_TRIGGER_HTTP="${TRIGGER_HTTP}" \
ZLINK_JAVA_E2E_SCENARIO="${SCENARIO}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

cat "${log_dir}/client.stdout.log"
if [[ "${SCENARIO}" == "all" ]]; then
  grep -q "scenario MON-A1 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-A2 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-A3 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-A4 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-A5 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-B1 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-B2 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-C1 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario MON-D1 passed" "${log_dir}/client.stdout.log"
else
  grep -q "scenario ${SCENARIO} passed" "${log_dir}/client.stdout.log"
fi
if compgen -G "${log_dir}/*-flow.log" >/dev/null; then
  grep -Rq "message flow" "${log_dir}"/*-flow.log
fi
