#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.registrationcodec\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/RegistrationCodec}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/RegistrationCodec-gradle-cache}"

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
    for _ in range(3):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(f"tcp://127.0.0.1:{ports[0]} http://127.0.0.1:{ports[1]} tcp://127.0.0.1:{ports[2]}")
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
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/install/registration-codec/bin/registration-codec"
}

read -r SERVER_ENDPOINT HTTP_ENDPOINT INVALID_ENDPOINT <<<"$(reserve_ports)"

gradle_run installDist

set +e
ZLINK_JAVA_E2E_ROLE=invalid-server \
ZLINK_JAVA_E2E_SERVER_ENDPOINT="${INVALID_ENDPOINT}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/invalid-server.stdout.log" 2>"${log_dir}/invalid-server.stderr.log"
invalid_status="$?"
set -e
if [[ "${invalid_status}" == "0" ]]; then
  echo "invalid registration server unexpectedly started" >&2
  exit 1
fi
cat "${log_dir}/invalid-server.stdout.log" "${log_dir}/invalid-server.stderr.log" \
  | grep -Eq "duplicate|Duplicate|registration|packet"
echo "scenario RC-A6 passed"

ZLINK_JAVA_E2E_ROLE=server \
ZLINK_JAVA_E2E_SERVER_ENDPOINT="${SERVER_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${HTTP_ENDPOINT}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/server.stdout.log" 2>"${log_dir}/server.stderr.log" &
pids+=("$!")
wait_port server "${SERVER_ENDPOINT}"
wait_port evidence "${HTTP_ENDPOINT}"
sleep 1

ZLINK_JAVA_E2E_ROLE=client \
ZLINK_JAVA_E2E_SERVER_ENDPOINT="${SERVER_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${HTTP_ENDPOINT}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(app_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

cat "${log_dir}/client.stdout.log"
python3 - "${HTTP_ENDPOINT}/evidence" >"${log_dir}/server-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY

grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "EchoAuto" "${log_dir}/server-evidence.json"
grep -q "ProtobufEcho" "${log_dir}/server-evidence.json"
grep -q "MsgpackEcho" "${log_dir}/server-evidence.json"
