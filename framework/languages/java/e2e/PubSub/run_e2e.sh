#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.pubsub\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/PubSub}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/PubSub-gradle-cache}"

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
    for _ in range(6):
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
  for _ in $(seq 1 600); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for marker ${file}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

app_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/install/pub-sub/bin/pub-sub"
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

start_subscriber() {
  local rid="$1"
  local topics="$2"
  local http="$3"
  local delay="${4:-}"
  ZLINK_JAVA_E2E_ROLE=subscriber \
  ZLINK_JAVA_E2E_SUBSCRIBER_RID="${rid}" \
  ZLINK_JAVA_E2E_TOPICS="${topics}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_JAVA_E2E_HANDLER_DELAY_MS="${delay}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${rid}-http" "${http}"
}

stop_pid() {
  local pid="$1"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

run_publisher_mode() {
  local mode="$1"
  local suffix="$2"
  ZLINK_JAVA_E2E_ROLE=publisher \
  ZLINK_JAVA_E2E_CLIENT_MODE="${mode}" \
  ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT="${PUBLISHER_ENDPOINT}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_SUB1_HTTP="${SUB1_HTTP}" \
  ZLINK_JAVA_E2E_SUB2_HTTP="${SUB2_HTTP}" \
  ZLINK_JAVA_E2E_SUB3_HTTP="${SUB3_HTTP}" \
  ZLINK_JAVA_E2E_LATE_CONTINUE_FILE="${LATE_CONTINUE}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/publisher-${suffix}.stdout.log" 2>"${log_dir}/publisher-${suffix}.stderr.log"
  cat "${log_dir}/publisher-${suffix}.stdout.log"
}

read -r REGISTRY_PUB REGISTRY_ROUTER PUBLISHER_ENDPOINT SUB1_HTTP SUB2_HTTP SUB3_HTTP <<<"$(reserve_ports)"

gradle_run installDist

PUBLISHER_READY="${log_dir}/publisher-ready"
PRELATE_CONTINUE="${log_dir}/prelate-continue"
LATE_READY="${log_dir}/late-ready"
LATE_CONTINUE="${log_dir}/late-continue"

start_registry

ZLINK_JAVA_E2E_ROLE=publisher \
ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT="${PUBLISHER_ENDPOINT}" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_SUB1_HTTP="${SUB1_HTTP}" \
ZLINK_JAVA_E2E_SUB2_HTTP="${SUB2_HTTP}" \
ZLINK_JAVA_E2E_SUB3_HTTP="${SUB3_HTTP}" \
ZLINK_JAVA_E2E_PUBLISHER_READY_FILE="${PUBLISHER_READY}" \
ZLINK_JAVA_E2E_PRELATE_CONTINUE_FILE="${PRELATE_CONTINUE}" \
ZLINK_JAVA_E2E_LATE_READY_FILE="${LATE_READY}" \
ZLINK_JAVA_E2E_LATE_CONTINUE_FILE="${LATE_CONTINUE}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/publisher.stdout.log" 2>"${log_dir}/publisher.stderr.log" &
CLIENT_PID="$!"
pids+=("${CLIENT_PID}")

wait_marker "${PUBLISHER_READY}"
wait_port publisher "${PUBLISHER_ENDPOINT}"
start_subscriber sub-1 alpha "${SUB1_HTTP}"
SUB1_PID="${LAST_PID}"
start_subscriber sub-2 beta "${SUB2_HTTP}"
SUB2_PID="${LAST_PID}"
sleep 2
touch "${PRELATE_CONTINUE}"

wait_marker "${LATE_READY}"
start_subscriber sub-3 gamma "${SUB3_HTTP}"
SUB3_PID="${LAST_PID}"
sleep 2
touch "${LATE_CONTINUE}"

wait "${CLIENT_PID}"
cat "${log_dir}/publisher.stdout.log"

stop_pid "${SUB1_PID}"
rm -f "${LATE_CONTINUE}"
run_publisher_mode subscriber-restarted ps-a4 &
PS_A4_PID="$!"
sleep 1
start_subscriber sub-1 alpha "${SUB1_HTTP}"
SUB1_PID="${LAST_PID}"
sleep 2
touch "${LATE_CONTINUE}"
wait "${PS_A4_PID}"

stop_pid "${SUB1_PID}"
start_subscriber sub-1 alpha "${SUB1_HTTP}" 750
SUB1_PID="${LAST_PID}"
sleep 2
run_publisher_mode slow-subscriber ps-b1

run_publisher_mode publisher-restarted ps-b2

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
grep -q "HANDLER_MISSING/DROP/MissingEventNotify" "${log_dir}/sub-2-evidence.json"
