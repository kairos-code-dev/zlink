#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.monitoring\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/Monitoring}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/Monitoring-gradle-cache}"

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
    for _ in range(7):
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
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

app_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/install/monitoring/bin/monitoring"
}

read -r REG_PUB_PORT REG_ROUTER_PORT REG_HTTP_PORT API_PORT SPOT_PORT SPOT_PUB_PORT SVC_HTTP_PORT <<<"$(reserve_ports)"
REGISTRY_PUB="$(tcp "${REG_PUB_PORT}")"
REGISTRY_ROUTER="$(tcp "${REG_ROUTER_PORT}")"
REGISTRY_HTTP="$(http "${REG_HTTP_PORT}")"
API_ENDPOINT="$(tcp "${API_PORT}")"
SPOT_ENDPOINT="$(tcp "${SPOT_PORT}")"
SPOT_PUB_ENDPOINT="$(tcp "${SPOT_PUB_PORT}")"
SERVICE_HTTP="$(http "${SVC_HTTP_PORT}")"

gradle_run installDist

ZLINK_JAVA_E2E_ROLE=registry \
ZLINK_JAVA_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${REGISTRY_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
pids+=("$!")
wait_port registry-router "${REGISTRY_ROUTER}"
wait_port registry-http "${REGISTRY_HTTP}"

ZLINK_JAVA_E2E_ROLE=service \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT="${SPOT_PUB_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${SERVICE_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/service.stdout.log" 2>"${log_dir}/service.stderr.log" &
pids+=("$!")
wait_port service-api "${API_ENDPOINT}"
wait_port service-http "${SERVICE_HTTP}"
sleep 2

ZLINK_JAVA_E2E_ROLE=client \
ZLINK_JAVA_E2E_API_ENDPOINT="${API_ENDPOINT}" \
ZLINK_JAVA_E2E_REGISTRY_HTTP="${REGISTRY_HTTP}" \
ZLINK_JAVA_E2E_SERVICE_HTTP="${SERVICE_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

ZLINK_JAVA_E2E_ROLE=validation \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/validation.stdout.log" 2>"${log_dir}/validation.stderr.log"

cat "${log_dir}/client.stdout.log"
cat "${log_dir}/validation.stdout.log"
grep -q "scenario MON-A1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-A2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-A3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-B1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-C1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario MON-B2 passed" "${log_dir}/validation.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
