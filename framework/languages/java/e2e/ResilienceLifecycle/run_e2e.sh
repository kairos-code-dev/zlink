#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.resiliencelifecycle\.(client|consumer|provider|registry)\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
SCENARIO="${1:-all}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/ResilienceLifecycle}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/ResilienceLifecycle-gradle-cache}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30

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
    for _ in range(9):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:5]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[5:]))
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

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/resilience-lifecycle-client/bin/resilience-lifecycle-client"
}

registry_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Registry/install/resilience-lifecycle-registry/bin/resilience-lifecycle-registry"
}

start_registry() {
  ZLINK_JAVA_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(registry_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
}

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B API_A_REPLACEMENT HTTP_A HTTP_B HTTP_A_REPLACEMENT _ <<<"$(reserve_ports)"

gradle_run installDist

start_registry

ZLINK_JAVA_E2E_CLIENT_MODE="suite" \
ZLINK_JAVA_E2E_SCENARIO="${SCENARIO}" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_API_A_ENDPOINT="${API_A}" \
ZLINK_JAVA_E2E_API_B_ENDPOINT="${API_B}" \
ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT="${API_A_REPLACEMENT}" \
ZLINK_JAVA_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
ZLINK_JAVA_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_JAVA_E2E_HTTP_A_REPLACEMENT_ENDPOINT="${HTTP_A_REPLACEMENT}" \
ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

cat "${log_dir}/client.stdout.log"
cat "${log_dir}"/consumer-*.stdout.log
if [[ "${SCENARIO}" == "all" ]]; then
  grep -q "scenario RL-A1 passed" "${log_dir}/consumer-restart.stdout.log"
  grep -q "scenario RL-A2 passed" "${log_dir}/consumer-reschedule.stdout.log"
  grep -q "scenario RL-A3 passed" "${log_dir}"/consumer-storm-*.stdout.log
  grep -q "scenario RL-A5 passed" "${log_dir}/consumer-flapping.stdout.log"
  grep -q "scenario RL-B1 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B3 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B4 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B5 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B6 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-C1 passed" "${log_dir}/consumer-cleanup.stdout.log"
  grep -q "scenario RL-C3 passed" "${log_dir}/consumer-restart.stdout.log"
  grep -q "scenario RL-D1 passed" "${log_dir}"/consumer-storm-*.stdout.log
  grep -q "scenario RL-D3 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-D5 passed" "${log_dir}/consumer-cleanup.stdout.log"
else
  grep -Rq "scenario ${SCENARIO} passed" "${log_dir}"/consumer-*.stdout.log
fi
grep -q "resilience-lifecycle e2e result=passed" "${log_dir}/client.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
