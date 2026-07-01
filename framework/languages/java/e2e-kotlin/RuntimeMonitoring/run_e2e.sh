#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.kotlin\.runtimemonitoring\.(client|registry|service|filteredservice|throwingservice|trigger)\.ProgramKt'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/RuntimeMonitoring}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/RuntimeMonitoring-gradle-cache}"
JVM_ROLE_READY_ATTEMPTS=100
PROCESS_READY_INTERVAL=0.1
ROUTE_SETTLE_SECONDS=5

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
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
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
    for _ in range(15):
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
  for _ in $(seq 1 "${JVM_ROLE_READY_ATTEMPTS}"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "${PROCESS_READY_INTERVAL}"
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_KOTLIN_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

client_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Client/install/runtime-monitoring-kotlin-client/bin/runtime-monitoring-kotlin-client"
}

registry_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Registry/install/runtime-monitoring-kotlin-registry/bin/runtime-monitoring-kotlin-registry"
}

service_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Service/install/runtime-monitoring-kotlin-service/bin/runtime-monitoring-kotlin-service"
}

filtered_service_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-FilteredService/install/runtime-monitoring-kotlin-filtered-service/bin/runtime-monitoring-kotlin-filtered-service"
}

throwing_service_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-ThrowingService/install/runtime-monitoring-kotlin-throwing-service/bin/runtime-monitoring-kotlin-throwing-service"
}

trigger_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Trigger/install/runtime-monitoring-kotlin-trigger/bin/runtime-monitoring-kotlin-trigger"
}

read -r REG_PUB_PORT REG_ROUTER_PORT REG_HTTP_PORT API_PORT HANDSHAKE_PORT SPOT_PORT SPOT_PUB_PORT SVC_HTTP_PORT FILTERED_API_PORT FILTERED_HTTP_PORT THROWING_API_PORT THROWING_HTTP_PORT TRIGGER_HTTP_PORT _ _ <<<"$(reserve_ports)"
REGISTRY_PUB="$(tcp "${REG_PUB_PORT}")"
REGISTRY_ROUTER="$(tcp "${REG_ROUTER_PORT}")"
REGISTRY_HTTP="$(http "${REG_HTTP_PORT}")"
API_ENDPOINT="$(tcp "${API_PORT}")"
HANDSHAKE_ENDPOINT="$(tcp "${HANDSHAKE_PORT}")"
SPOT_ENDPOINT="$(tcp "${SPOT_PORT}")"
SPOT_PUB_ENDPOINT="$(tcp "${SPOT_PUB_PORT}")"
SERVICE_HTTP="$(http "${SVC_HTTP_PORT}")"
FILTERED_API_ENDPOINT="$(tcp "${FILTERED_API_PORT}")"
FILTERED_SERVICE_HTTP="$(http "${FILTERED_HTTP_PORT}")"
THROWING_API_ENDPOINT="$(tcp "${THROWING_API_PORT}")"
THROWING_SERVICE_HTTP="$(http "${THROWING_HTTP_PORT}")"
TRIGGER_HTTP="$(http "${TRIGGER_HTTP_PORT}")"

gradle_run clean :Client:installDist :Server:Registry:installDist :Server:Service:installDist :Server:FilteredService:installDist :Server:ThrowingService:installDist :Server:Trigger:installDist

ZLINK_KOTLIN_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${REGISTRY_HTTP}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(registry_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
pids+=("$!")
wait_port registry-router "${REGISTRY_ROUTER}"
wait_port registry-http "${REGISTRY_HTTP}"

ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_RID="svc-a" \
ZLINK_KOTLIN_E2E_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_HANDSHAKE_ENDPOINT="${HANDSHAKE_ENDPOINT}" \
ZLINK_KOTLIN_E2E_SPOT_ENDPOINT="${SPOT_ENDPOINT}" \
ZLINK_KOTLIN_E2E_SPOT_PUB_ENDPOINT="${SPOT_PUB_ENDPOINT}" \
ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${SERVICE_HTTP}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(service_bin)" >"${log_dir}/service.stdout.log" 2>"${log_dir}/service.stderr.log" &
pids+=("$!")
wait_port service-api "${API_ENDPOINT}"
wait_port service-http "${SERVICE_HTTP}"

ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_RID="svc-b" \
ZLINK_KOTLIN_E2E_API_ENDPOINT="${FILTERED_API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${FILTERED_SERVICE_HTTP}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(filtered_service_bin)" >"${log_dir}/filtered-service.stdout.log" 2>"${log_dir}/filtered-service.stderr.log" &
pids+=("$!")
wait_port filtered-service-api "${FILTERED_API_ENDPOINT}"
wait_port filtered-service-http "${FILTERED_SERVICE_HTTP}"

ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_API_ENDPOINT="${THROWING_API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${THROWING_SERVICE_HTTP}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(throwing_service_bin)" >"${log_dir}/throwing-service.stdout.log" 2>"${log_dir}/throwing-service.stderr.log" &
pids+=("$!")
wait_port throwing-service-api "${THROWING_API_ENDPOINT}"
wait_port throwing-service-http "${THROWING_SERVICE_HTTP}"

ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${TRIGGER_HTTP}" \
ZLINK_KOTLIN_E2E_SERVICE_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_FILTERED_API_ENDPOINT="${FILTERED_API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_THROWING_API_ENDPOINT="${THROWING_API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(trigger_bin)" >"${log_dir}/trigger.stdout.log" 2>"${log_dir}/trigger.stderr.log" &
pids+=("$!")
wait_port trigger-http "${TRIGGER_HTTP}"
sleep "${ROUTE_SETTLE_SECONDS}"

ZLINK_KOTLIN_E2E_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_HANDSHAKE_ENDPOINT="${HANDSHAKE_ENDPOINT}" \
ZLINK_KOTLIN_E2E_FILTERED_API_ENDPOINT="${FILTERED_API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_THROWING_API_ENDPOINT="${THROWING_API_ENDPOINT}" \
ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_REGISTRY_HTTP="${REGISTRY_HTTP}" \
ZLINK_KOTLIN_E2E_SERVICE_HTTP="${SERVICE_HTTP}" \
ZLINK_KOTLIN_E2E_FILTERED_SERVICE_HTTP="${FILTERED_SERVICE_HTTP}" \
ZLINK_KOTLIN_E2E_THROWING_SERVICE_HTTP="${THROWING_SERVICE_HTTP}" \
ZLINK_KOTLIN_E2E_TRIGGER_HTTP="${TRIGGER_HTTP}" \
ZLINK_KOTLIN_E2E_FILTERED_SERVICE_BIN="$(filtered_service_bin)" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

cat "${log_dir}/client.stdout.log"
grep -q "scenario MON-A1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-A2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-A3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-A4 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-A5 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-B1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-B2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-C1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-D1 passed" "${log_dir}/client.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
