#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.registrymessaging\.(client|provider|registry|workflow|consumer)\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/RegistryMessaging}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/RegistryMessaging-gradle-cache}"

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

kill_role_processes() {
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill "${pid}" >/dev/null 2>&1 || true
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
  kill_role_processes
  sleep 0.5
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    local pid="${pids[$i]}"
    for child in $(descendants "${pid}"); do
      kill -9 "${child}" >/dev/null 2>&1 || true
    done
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
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports))
finally:
    for sock in sockets:
        sock.close()
PY
}

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_health() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 600); do
    if python3 - "http://127.0.0.1:${port}/health" <<'PY'
import sys
import urllib.request

try:
    with urllib.request.urlopen(sys.argv[1], timeout=0.2) as response:
        sys.exit(0 if 200 <= response.status < 300 else 1)
except Exception:
    sys.exit(1)
PY
    then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} health at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}


client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/registry-messaging-client/bin/registry-messaging-client"
}

provider_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Provider/install/registry-messaging-provider/bin/registry-messaging-provider"
}

registry_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Registry/install/registry-messaging-registry/bin/registry-messaging-registry"
}

workflow_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Workflow/install/registry-messaging-workflow/bin/registry-messaging-workflow"
}

consumer_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Consumer/install/registry-messaging-consumer/bin/registry-messaging-consumer"
}

start_registry() {
  ZLINK_JAVA_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_HTTP_PORT="$(port_of "${REGISTRY_HTTP}")" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(registry_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
  wait_health registry "${REGISTRY_HTTP}"
}

start_provider() {
  local rid="$1"
  local api="$2"
  local route="$3"
  local workflow="$4"
  local instance="${5:-$rid}"
  local weight="${6:-}"
  local http_port="${7:?http port is required}"
  local binary
  if [[ -n "${workflow}" && -z "${api}" && -z "${route}" ]]; then
    binary="$(workflow_bin)"
  else
    binary="$(provider_bin)"
  fi
  ZLINK_JAVA_E2E_PROVIDER_RID="${rid}" \
  ZLINK_JAVA_E2E_PROVIDER_INSTANCE="${instance}" \
  ZLINK_JAVA_E2E_API_WEIGHT="${weight}" \
  ZLINK_JAVA_E2E_API_ENDPOINT="${api}" \
  ZLINK_JAVA_E2E_API_MANUAL_ENDPOINT="${API_A}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_JAVA_E2E_ROUTE_PEERS="${ROUTE_B}" \
  ZLINK_JAVA_E2E_WORKFLOW_ENDPOINT="${workflow}" \
  ZLINK_JAVA_E2E_HTTP_PORT="$(port_of "${http_port}")" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "${binary}" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  [[ -z "${api}" ]] || wait_port "${rid}-api" "${api}"
  [[ -z "${route}" ]] || wait_port "${rid}-route" "${route}"
  [[ -z "${workflow}" ]] || wait_port "${rid}-workflow" "${workflow}"
  wait_health "${rid}" "${http_port}"
}

start_consumer() {
  local name="$1"
  local mode="$2"
  local http_port="$3"
  local endpoints="${4:-}"
  ZLINK_JAVA_E2E_CONSUMER_NAME="${name}" \
  ZLINK_JAVA_E2E_CONSUMER_MODE="${mode}" \
  ZLINK_JAVA_E2E_PROVIDER_ENDPOINTS="${endpoints}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_HTTP_PORT="$(port_of "${http_port}")" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(consumer_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  pids+=("$!")
  wait_health "${name}" "${http_port}"
}

stop_pid() {
  local pid="$1"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

run_client() {
  local scenario="$1"
  local suffix="$2"
  shift 2
  ZLINK_JAVA_E2E_SCENARIO="${scenario}" \
  ZLINK_JAVA_E2E_REGISTRY_HTTP_URL="http://127.0.0.1:$(port_of "${REGISTRY_HTTP}")" \
  ZLINK_JAVA_E2E_PROVIDER_A_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_API_A}")" \
  ZLINK_JAVA_E2E_PROVIDER_B_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_API_B}")" \
  ZLINK_JAVA_E2E_WORKFLOW_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_WORKFLOW}")" \
  ZLINK_JAVA_E2E_DISCOVERY_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_DISCOVERY_CONSUMER}")" \
  ZLINK_JAVA_E2E_DIRECT_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_DIRECT_CONSUMER}")" \
  ZLINK_JAVA_E2E_SINGLE_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_SINGLE_CONSUMER}")" \
  ZLINK_JAVA_E2E_BACKPRESSURE_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_BACKPRESSURE_CONSUMER}")" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$@" \
    "$(client_bin)" >"${log_dir}/client-${suffix}.stdout.log" 2>"${log_dir}/client-${suffix}.stderr.log"
}

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B ROUTE_A ROUTE_B WORKFLOW_A REGISTRY_HTTP HTTP_API_A HTTP_API_B HTTP_WORKFLOW HTTP_DISCOVERY_CONSUMER HTTP_DIRECT_CONSUMER HTTP_SINGLE_CONSUMER HTTP_BACKPRESSURE_CONSUMER <<<"$(reserve_ports)"

gradle_run installDist

start_registry

start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a "" "${HTTP_API_A}"
API_A_PID="${LAST_PID}"
start_provider api-b "${API_B}" "${ROUTE_B}" "" api-b "" "${HTTP_API_B}"
API_B_PID="${LAST_PID}"
start_provider workflow-a "" "" "${WORKFLOW_A}" workflow-a "" "${HTTP_WORKFLOW}"
WORKFLOW_A_PID="${LAST_PID}"
start_consumer discovery-consumer discovery "${HTTP_DISCOVERY_CONSUMER}"
start_consumer direct-consumer direct "${HTTP_DIRECT_CONSUMER}" "${API_A},${API_B}"
start_consumer single-consumer direct "${HTTP_SINGLE_CONSUMER}" "${API_A}"
start_consumer backpressure-consumer direct "${HTTP_BACKPRESSURE_CONSUMER}" "${API_A}"
sleep 2
run_client common common env
cat "${log_dir}/client-common.stdout.log"
stop_pid "${API_A_PID}"
stop_pid "${API_B_PID}"
stop_pid "${WORKFLOW_A_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a 75 "${HTTP_API_A}"
API_A_PID="${LAST_PID}"
start_provider api-b "${API_B}" "${ROUTE_B}" "" api-b 25 "${HTTP_API_B}"
API_B_PID="${LAST_PID}"
sleep 2
run_client weighted rm-c7 env
cat "${log_dir}/client-rm-c7.stdout.log"
stop_pid "${API_A_PID}"
stop_pid "${API_B_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a "" "${HTTP_API_A}"
API_A_PID="${LAST_PID}"
run_client scale-out rm-b1 env
cat "${log_dir}/client-rm-b1.stdout.log"
stop_pid "${API_A_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a "" "${HTTP_API_A}"
API_A_PID="${LAST_PID}"
start_provider api-b "${API_B}" "${ROUTE_B}" "" api-b "" "${HTTP_API_B}"
API_B_PID="${LAST_PID}"
run_client scale-in rm-b2 env
cat "${log_dir}/client-rm-b2.stdout.log"
stop_pid "${API_A_PID}"
stop_pid "${API_B_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a-v1 "" "${HTTP_API_A}"
API_A_PID="${LAST_PID}"
run_client failover rm-a4 env
cat "${log_dir}/client-rm-a4.stdout.log"
stop_pid "${API_A_PID}"

grep -Rq "message flow" "${log_dir}"/*-flow.log
