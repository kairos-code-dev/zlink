#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='resilience-lifecycle-kotlin-(client|registry|provider)'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/ResilienceLifecycle}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/ResilienceLifecycle-gradle-cache}"

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
    for _ in range(8):
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
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_port_down() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 200); do
    if ! (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} to stop at ${endpoint}" >&2
  return 1
}

wait_file() {
  local file="$1"
  for _ in $(seq 1 300); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${file}" >&2
  return 1
}

stop_pid() {
  local pid="$1"
  for child in $(descendants "${pid}"); do
    kill "${child}" >/dev/null 2>&1 || true
  done
  kill "${pid}" >/dev/null 2>&1 || true
  wait "${pid}" >/dev/null 2>&1 || true
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_KOTLIN_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

client_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Client/install/resilience-lifecycle-kotlin-client/bin/resilience-lifecycle-kotlin-client"
}

registry_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Registry/install/resilience-lifecycle-kotlin-registry/bin/resilience-lifecycle-kotlin-registry"
}

provider_bin() {
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Provider/install/resilience-lifecycle-kotlin-provider/bin/resilience-lifecycle-kotlin-provider"
}

start_registry() {
  ZLINK_KOTLIN_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(registry_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
}

start_provider() {
  local rid="$1"
  local api="$2"
  local http="$3"
  ZLINK_KOTLIN_E2E_PROVIDER_RID="${rid}" \
  ZLINK_KOTLIN_E2E_API_ENDPOINT="${api}" \
  ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(provider_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  pids+=("$!")
  wait_port "${rid}-api" "${api}"
  wait_port "${rid}-http" "${http}"
}

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B API_A_REPLACEMENT HTTP_A HTTP_B HTTP_A_REPLACEMENT <<<"$(reserve_ports)"

gradle_run clean installDist

start_registry
REGISTRY_PID="${pids[-1]}"
start_provider api-a "${API_A}" "${HTTP_A}"
PROVIDER_A_PID="${pids[-1]}"
start_provider api-b "${API_B}" "${HTTP_B}"
PROVIDER_B_PID="${pids[-1]}"
sleep 2

control_dir="${log_dir}/control"
mkdir -p "${control_dir}"

ZLINK_KOTLIN_E2E_CLIENT_MODE=restart \
ZLINK_KOTLIN_E2E_CONTROL_DIR="${control_dir}" \
ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client-restart.stdout.log" 2>"${log_dir}/client-restart.stderr.log" &
restart_client_pid="$!"
pids+=("${restart_client_pid}")

wait_file "${control_dir}/a1-ready"
stop_pid "${PROVIDER_A_PID}"
wait_port_down api-a "${API_A}"
touch "${control_dir}/a1-down"
wait_file "${control_dir}/a1-down-observed"
start_provider api-a "${API_A}" "${HTTP_A}"
PROVIDER_A_PID="${pids[-1]}"
sleep 2
touch "${control_dir}/a1-up"
wait "${restart_client_pid}"

ZLINK_KOTLIN_E2E_CLIENT_MODE=reschedule \
ZLINK_KOTLIN_E2E_CONTROL_DIR="${control_dir}" \
ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_API_A_REPLACEMENT_ENDPOINT="${API_A_REPLACEMENT}" \
ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client-reschedule.stdout.log" 2>"${log_dir}/client-reschedule.stderr.log" &
reschedule_client_pid="$!"
pids+=("${reschedule_client_pid}")

wait_file "${control_dir}/a2-ready"
stop_pid "${PROVIDER_A_PID}"
wait_port_down api-a "${API_A}"
touch "${control_dir}/a2-down"
wait_file "${control_dir}/a2-down-observed"
start_provider api-a "${API_A_REPLACEMENT}" "${HTTP_A_REPLACEMENT}"
PROVIDER_A_PID="${pids[-1]}"
sleep 2
touch "${control_dir}/a2-up"
wait "${reschedule_client_pid}"

ZLINK_KOTLIN_E2E_CLIENT_MODE=flapping \
ZLINK_KOTLIN_E2E_CONTROL_DIR="${control_dir}" \
ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A_REPLACEMENT}" \
ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client-flapping.stdout.log" 2>"${log_dir}/client-flapping.stderr.log" &
flapping_client_pid="$!"
pids+=("${flapping_client_pid}")

wait_file "${control_dir}/a5-ready"
for _ in $(seq 1 3); do
  stop_pid "${PROVIDER_A_PID}"
  wait_port_down api-a "${API_A_REPLACEMENT}"
  sleep 1
  start_provider api-a "${API_A_REPLACEMENT}" "${HTTP_A_REPLACEMENT}"
  PROVIDER_A_PID="${pids[-1]}"
  sleep 2
done
touch "${control_dir}/a5-stop"
wait "${flapping_client_pid}"

ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A_REPLACEMENT}" \
ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client-default.stdout.log" 2>"${log_dir}/client-default.stderr.log"

wait_port_down api-b "${API_B}"
start_provider api-b "${API_B}" "${HTTP_B}"
PROVIDER_B_PID="${pids[-1]}"
sleep 3

for wave in 1 2; do
  storm_pids=()
  for index in $(seq 1 6); do
    storm_log_dir="${log_dir}/storm-${wave}-${index}"
    mkdir -p "${storm_log_dir}"
    ZLINK_KOTLIN_E2E_CLIENT_MODE=storm \
    ZLINK_KOTLIN_E2E_CONTROL_DIR="${control_dir}" \
    ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
    ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A_REPLACEMENT}" \
    ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
    ZLINK_KOTLIN_E2E_STORM_EXIT_DELAY_MS="$((index * 250))" \
    ZLINK_KOTLIN_E2E_LOG_DIR="${storm_log_dir}" \
      "$(client_bin)" >"${log_dir}/client-storm-${wave}-${index}.stdout.log" 2>"${log_dir}/client-storm-${wave}-${index}.stderr.log" &
    storm_pids+=("$!")
    pids+=("$!")
  done
  for pid in "${storm_pids[@]}"; do
    wait "${pid}"
  done
done

ZLINK_KOTLIN_E2E_CLIENT_MODE=cleanup \
ZLINK_KOTLIN_E2E_CONTROL_DIR="${control_dir}" \
ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A_REPLACEMENT}" \
ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client-cleanup.stdout.log" 2>"${log_dir}/client-cleanup.stderr.log"

cat "${log_dir}/client-restart.stdout.log"
cat "${log_dir}/client-reschedule.stdout.log"
cat "${log_dir}/client-flapping.stdout.log"
cat "${log_dir}"/client-storm-*.stdout.log
cat "${log_dir}/client-cleanup.stdout.log"
cat "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-A1 passed" "${log_dir}/client-restart.stdout.log"
grep -q "scenario RL-A2 passed" "${log_dir}/client-reschedule.stdout.log"
grep -q "scenario RL-A3 passed" "${log_dir}"/client-storm-*.stdout.log
grep -q "scenario RL-A5 passed" "${log_dir}/client-flapping.stdout.log"
grep -q "scenario RL-B1 passed" "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-B3 passed" "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-B4 passed" "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-B5 passed" "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-B6 passed" "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-C1 passed" "${log_dir}/client-cleanup.stdout.log"
grep -q "scenario RL-C3 passed" "${log_dir}/client-restart.stdout.log"
grep -q "scenario RL-D1 passed" "${log_dir}"/client-storm-*.stdout.log
grep -q "scenario RL-D3 passed" "${log_dir}/client-default.stdout.log"
grep -q "scenario RL-D5 passed" "${log_dir}/client-cleanup.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
