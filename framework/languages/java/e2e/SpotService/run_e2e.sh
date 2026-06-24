#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.spotservice\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/SpotService}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/SpotService-gradle-cache}"

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
    for _ in range(12):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:10]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[10:]))
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

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

app_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/install/spot-service/bin/spot-service"
}

start_registry() {
  ZLINK_JAVA_E2E_ROLE=registry \
  ZLINK_JAVA_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
}

start_play() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local ingress="$4"
  local http="$5"
  ZLINK_JAVA_E2E_ROLE=play \
  ZLINK_JAVA_E2E_NODE_RID="${rid}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_JAVA_E2E_INGRESS_ENDPOINT="${ingress}" \
  ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_JAVA_E2E_SPOT_ENDPOINT="${spot}" \
  ZLINK_JAVA_E2E_SPOT_A_ENDPOINT="${SPOT_A}" \
  ZLINK_JAVA_E2E_SPOT_B_ENDPOINT="${SPOT_B}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  pids+=("$!")
  wait_port "${rid}-route" "${route}"
  wait_port "${rid}-spot" "${spot}"
  wait_port "${rid}-http" "${http}"
}

fetch_evidence() {
  local name="$1"
  local endpoint="$2"
  python3 - "${endpoint}/evidence" >"${log_dir}/${name}-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
}

close_spot() {
  local endpoint="$1"
  local rid="$2"
  python3 - "${endpoint}/admin/close?rid=${rid}" <<'PY'
import sys
import urllib.request
request = urllib.request.Request(sys.argv[1], method="POST")
with urllib.request.urlopen(request, timeout=5) as response:
    body = response.read().decode("utf-8")
    if '"closed":true' not in body:
        raise SystemExit("spot close did not report closed=true: " + body)
PY
}

assert_type_mismatch() {
  local endpoint="$1"
  local rid="$2"
  python3 - "${endpoint}/admin/type-mismatch?rid=${rid}" <<'PY'
import sys
import urllib.request
request = urllib.request.Request(sys.argv[1], method="POST")
with urllib.request.urlopen(request, timeout=5) as response:
    body = response.read().decode("utf-8")
    if '"mismatch":true' not in body:
        raise SystemExit("spot type mismatch did not report mismatch=true: " + body)
PY
}

read -r REGISTRY_PUB REGISTRY_ROUTER ROUTE_A ROUTE_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_CLIENT INGRESS_A INGRESS_B HTTP_A HTTP_B <<<"$(reserve_ports)"

gradle_run installDist

start_registry
start_play play-a "${ROUTE_A}" "${SPOT_A}" "${INGRESS_A}" "${HTTP_A}"
start_play play-b "${ROUTE_B}" "${SPOT_B}" "${INGRESS_B}" "${HTTP_B}"
sleep 2

run_client_mode() {
  local mode="$1"
  ZLINK_JAVA_E2E_ROLE=client \
  ZLINK_JAVA_E2E_CLIENT_MODE="${mode}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_CLIENT}" \
  ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_CLIENT}" \
  ZLINK_JAVA_E2E_SPOT_A_ENDPOINT="${SPOT_A}" \
  ZLINK_JAVA_E2E_SPOT_B_ENDPOINT="${SPOT_B}" \
  ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT="${INGRESS_A}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/client-${mode}.stdout.log" 2>"${log_dir}/client-${mode}.stderr.log"
  cat "${log_dir}/client-${mode}.stdout.log" >>"${log_dir}/client.stdout.log"
  cat "${log_dir}/client-${mode}.stderr.log" >>"${log_dir}/client.stderr.log"
}

: >"${log_dir}/client.stdout.log"
: >"${log_dir}/client.stderr.log"
for mode in state1 state2 send normal missing timeout owner route-mesh; do
  run_client_mode "${mode}"
  sleep 2
done
assert_type_mismatch "${HTTP_A}" room-a
echo "scenario SM-A7 passed" >>"${log_dir}/client.stdout.log"
echo "scenario SM-E2 passed" >>"${log_dir}/client.stdout.log"
close_spot "${HTTP_A}" room-a
echo "scenario SM-A6 passed" >>"${log_dir}/client.stdout.log"

cat "${log_dir}/client.stdout.log"
fetch_evidence play-a "${HTTP_A}"
fetch_evidence play-b "${HTTP_B}"
grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "DispatchError" "${log_dir}/play-a-evidence.json"
grep -q "SpotInitialized" "${log_dir}/play-a-evidence.json"
grep -q "SpotClosing" "${log_dir}/play-a-evidence.json"
grep -q "SpotTypeMismatch" "${log_dir}/play-a-evidence.json"
grep -q "SpotTypeMismatchStateOk" "${log_dir}/play-a-evidence.json"
grep -q "SpotTimer" "${log_dir}/play-a-evidence.json"
