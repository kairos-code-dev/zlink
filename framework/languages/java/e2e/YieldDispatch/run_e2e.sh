#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.yielddispatch\.(client|delay|play|registry|session)\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/YieldDispatch}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/YieldDispatch-gradle-cache}"

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
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_http() {
  local name="$1"
  local endpoint="$2"
  for _ in $(seq 1 600); do
    if python3 - "${endpoint}/evidence" >/dev/null 2>&1 <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=1) as response:
    response.read()
PY
    then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

fetch_evidence() {
  local endpoint="$1"
  local output="$2"
  python3 - "${endpoint}/evidence" >"${output}" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
}

wait_readiness() {
  : >"${log_dir}/readiness.stdout.log"
  : >"${log_dir}/readiness.stderr.log"
  for attempt in $(seq 1 60); do
    if ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
      ZLINK_JAVA_E2E_PLAY_HTTP="${PLAY_A_HTTP}" \
      ZLINK_JAVA_E2E_PLAY_B_HTTP="${PLAY_B_HTTP}" \
      ZLINK_JAVA_E2E_SESSION_HTTP="${SESSION_HTTP}" \
      ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
        timeout -k 5s 20s "$(client_bin)" --readiness \
        >>"${log_dir}/readiness.stdout.log" 2>>"${log_dir}/readiness.stderr.log"; then
      return 0
    fi
    printf 'readiness attempt %s failed\n' "${attempt}" >>"${log_dir}/readiness.stderr.log"
    sleep 0.5
  done
  echo "Timed out waiting for route and spot readiness" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon --no-parallel --max-workers=1 "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/yield-dispatch-client/bin/yield-dispatch-client"
}

registry_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Registry/install/yield-dispatch-registry/bin/yield-dispatch-registry"
}

delay_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Delay/install/yield-dispatch-delay/bin/yield-dispatch-delay"
}

play_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Play/install/yield-dispatch-play/bin/yield-dispatch-play"
}

session_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Session/install/yield-dispatch-session/bin/yield-dispatch-session"
}

read -r REG_PUB_PORT REG_ROUTER_PORT DELAY_PORT ROUTE_A_PORT SPOT_A_PORT ROUTE_B_PORT SPOT_B_PORT STREAM_PORT PLAY_A_HTTP_PORT PLAY_B_HTTP_PORT SESSION_HTTP_PORT SESSION_ROUTE_PORT SESSION_SPOT_PORT <<<"$(reserve_ports)"
REGISTRY_PUB="$(tcp "${REG_PUB_PORT}")"
REGISTRY_ROUTER="$(tcp "${REG_ROUTER_PORT}")"
DELAY_ENDPOINT="$(tcp "${DELAY_PORT}")"
ROUTE_A_ENDPOINT="$(tcp "${ROUTE_A_PORT}")"
SPOT_A_ENDPOINT="$(tcp "${SPOT_A_PORT}")"
ROUTE_B_ENDPOINT="$(tcp "${ROUTE_B_PORT}")"
SPOT_B_ENDPOINT="$(tcp "${SPOT_B_PORT}")"
STREAM_ENDPOINT="$(tcp "${STREAM_PORT}")"
PLAY_A_HTTP="$(http "${PLAY_A_HTTP_PORT}")"
PLAY_B_HTTP="$(http "${PLAY_B_HTTP_PORT}")"
SESSION_HTTP="$(http "${SESSION_HTTP_PORT}")"
SESSION_ROUTE_ENDPOINT="$(tcp "${SESSION_ROUTE_PORT}")"
SESSION_SPOT_ENDPOINT="$(tcp "${SESSION_SPOT_PORT}")"

gradle_run installDist

ZLINK_JAVA_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(registry_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
pids+=("$!")
wait_port registry-router "${REGISTRY_ROUTER}"

ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(delay_bin)" >"${log_dir}/delay.stdout.log" 2>"${log_dir}/delay.stderr.log" &
pids+=("$!")
wait_port delay "${DELAY_ENDPOINT}"

ZLINK_JAVA_E2E_NODE_RID="play-a" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_A_ENDPOINT}" \
ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT="${ROUTE_B_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_A_ENDPOINT}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${PLAY_A_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(play_bin)" >"${log_dir}/play-a.stdout.log" 2>"${log_dir}/play-a.stderr.log" &
pids+=("$!")
wait_port play-a-route "${ROUTE_A_ENDPOINT}"
wait_port play-a-spot "${SPOT_A_ENDPOINT}"
wait_http play-a-http "${PLAY_A_HTTP}"

ZLINK_JAVA_E2E_NODE_RID="play-b" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_B_ENDPOINT}" \
ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT="${ROUTE_A_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_B_ENDPOINT}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${PLAY_B_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(play_bin)" >"${log_dir}/play-b.stdout.log" 2>"${log_dir}/play-b.stderr.log" &
pids+=("$!")
wait_port play-b-route "${ROUTE_B_ENDPOINT}"
wait_port play-b-spot "${SPOT_B_ENDPOINT}"
wait_http play-b-http "${PLAY_B_HTTP}"

ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_A_ENDPOINT}" \
ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B_ENDPOINT}" \
ZLINK_JAVA_E2E_SESSION_ROUTE_ENDPOINT="${SESSION_ROUTE_ENDPOINT}" \
ZLINK_JAVA_E2E_SESSION_SPOT_ENDPOINT="${SESSION_SPOT_ENDPOINT}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${SESSION_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(session_bin)" >"${log_dir}/session.stdout.log" 2>"${log_dir}/session.stderr.log" &
pids+=("$!")
wait_port session-route "${SESSION_ROUTE_ENDPOINT}"
wait_port session-spot "${SESSION_SPOT_ENDPOINT}"
wait_port session-stream "${STREAM_ENDPOINT}"
wait_http session-http "${SESSION_HTTP}"
wait_readiness

ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
ZLINK_JAVA_E2E_PLAY_HTTP="${PLAY_A_HTTP}" \
ZLINK_JAVA_E2E_PLAY_B_HTTP="${PLAY_B_HTTP}" \
ZLINK_JAVA_E2E_SESSION_HTTP="${SESSION_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  timeout -k 5s 90s "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

fetch_evidence "${PLAY_A_HTTP}" "${log_dir}/play-a-evidence.json"
fetch_evidence "${PLAY_B_HTTP}" "${log_dir}/play-b-evidence.json"
fetch_evidence "${SESSION_HTTP}" "${log_dir}/session-evidence.json"
cat "${log_dir}/client.stdout.log"
grep -q "scenario YD-A1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-A2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-A3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-A4 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-B1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-B2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-B3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-C1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-C2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-C3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-D2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-D3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-D4 passed" "${log_dir}/client.stdout.log"
grep -q "yield-dispatch e2e result=passed" "${log_dir}/client.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
