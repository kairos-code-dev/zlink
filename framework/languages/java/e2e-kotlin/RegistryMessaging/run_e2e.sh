#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.kotlin\.registrymessaging\.ProgramKt'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/RegistryMessaging}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/RegistryMessaging-gradle-cache}"

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
    for _ in range(11):
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

wait_marker() {
  local file="$1"
  for _ in $(seq 1 400); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for marker ${file}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_KOTLIN_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

app_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/install/registry-messaging-kotlin/bin/registry-messaging-kotlin"
}

start_registry() {
  ZLINK_KOTLIN_E2E_ROLE=registry \
  ZLINK_KOTLIN_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
}

start_provider() {
  local rid="$1"
  local api="$2"
  local route="$3"
  local workflow="$4"
  local instance="${5:-$rid}"
  ZLINK_KOTLIN_E2E_ROLE=provider \
  ZLINK_KOTLIN_E2E_PROVIDER_RID="${rid}" \
  ZLINK_KOTLIN_E2E_PROVIDER_INSTANCE="${instance}" \
  ZLINK_KOTLIN_E2E_API_ENDPOINT="${api}" \
  ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_KOTLIN_E2E_WORKFLOW_ENDPOINT="${workflow}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  [[ -z "${api}" ]] || wait_port "${rid}-api" "${api}"
  [[ -z "${route}" ]] || wait_port "${rid}-route" "${route}"
  [[ -z "${workflow}" ]] || wait_port "${rid}-workflow" "${workflow}"
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
  ZLINK_KOTLIN_E2E_ROLE=client \
  ZLINK_KOTLIN_E2E_SCENARIO="${scenario}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_API_A_ENDPOINT="${API_A}" \
  ZLINK_KOTLIN_E2E_API_B_ENDPOINT="${API_B}" \
  ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_KOTLIN_E2E_CLIENT_ROUTE_ENDPOINT="${CLIENT_ROUTE}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$@" \
    "$(app_bin)" >"${log_dir}/client-${suffix}.stdout.log" 2>"${log_dir}/client-${suffix}.stderr.log"
}

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B ROUTE_A ROUTE_B WORKFLOW_A CLIENT_ROUTE API_A2 ROUTE_A2 UNUSED <<<"$(reserve_ports)"

gradle_run installDist

start_registry

start_provider api-a "${API_A}" "${ROUTE_A}" ""
API_A_PID="${LAST_PID}"
start_provider api-b "${API_B}" "${ROUTE_B}" ""
API_B_PID="${LAST_PID}"
start_provider workflow-a "" "" "${WORKFLOW_A}"
WORKFLOW_A_PID="${LAST_PID}"
sleep 2
run_client common common env
cat "${log_dir}/client-common.stdout.log"
stop_pid "${API_A_PID}"
stop_pid "${API_B_PID}"
stop_pid "${WORKFLOW_A_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" ""
API_A_PID="${LAST_PID}"
READY="${log_dir}/rm-b1-ready"
CONTINUE="${log_dir}/rm-b1-continue"
run_client scale-out rm-b1 env \
  ZLINK_KOTLIN_E2E_READY_FILE="${READY}" \
  ZLINK_KOTLIN_E2E_CONTINUE_FILE="${CONTINUE}" &
B1_CLIENT_PID="$!"
wait_marker "${READY}"
start_provider api-b "${API_B}" "${ROUTE_B}" ""
API_B_PID="${LAST_PID}"
sleep 5
touch "${CONTINUE}"
wait "${B1_CLIENT_PID}"
cat "${log_dir}/client-rm-b1.stdout.log"
stop_pid "${API_A_PID}"
stop_pid "${API_B_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" ""
API_A_PID="${LAST_PID}"
start_provider api-b "${API_B}" "${ROUTE_B}" ""
API_B_PID="${LAST_PID}"
READY="${log_dir}/rm-b2-ready"
CONTINUE="${log_dir}/rm-b2-continue"
run_client scale-in rm-b2 env \
  ZLINK_KOTLIN_E2E_READY_FILE="${READY}" \
  ZLINK_KOTLIN_E2E_CONTINUE_FILE="${CONTINUE}" &
B2_CLIENT_PID="$!"
wait_marker "${READY}"
stop_pid "${API_B_PID}"
sleep 5
touch "${CONTINUE}"
wait "${B2_CLIENT_PID}"
cat "${log_dir}/client-rm-b2.stdout.log"
stop_pid "${API_A_PID}"

start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a-v1
API_A_PID="${LAST_PID}"
READY="${log_dir}/rm-a4-ready"
CONTINUE="${log_dir}/rm-a4-continue"
run_client failover rm-a4 env \
  ZLINK_KOTLIN_E2E_READY_FILE="${READY}" \
  ZLINK_KOTLIN_E2E_CONTINUE_FILE="${CONTINUE}" &
A4_CLIENT_PID="$!"
wait_marker "${READY}"
stop_pid "${API_A_PID}"
start_provider api-a "${API_A2}" "${ROUTE_A2}" "" api-a-v2
API_A_PID="${LAST_PID}"
sleep 5
touch "${CONTINUE}"
wait "${A4_CLIENT_PID}"
cat "${log_dir}/client-rm-a4.stdout.log"
stop_pid "${API_A_PID}"

grep -Rq "message flow" "${log_dir}"/*-flow.log
