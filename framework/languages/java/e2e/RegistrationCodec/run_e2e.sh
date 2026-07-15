#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/e2e-redis-common.sh"

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
SCENARIO="${1:-all}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/RegistrationCodec}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/RegistrationCodec-gradle-cache}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_SECONDS=5
if [[ "${ROUTE_SETTLE_SECONDS:-}" != 5 ]]; then
  echo "RegistrationCodec must use a 5s route settle limit" >&2
  exit 1
fi
if rg -n 'java\.net\.http\.HttpClient|HttpClient\.new' \
    "$(pwd)/Client/src/main/java" --glob '*.java'; then
  echo "RegistrationCodec client must use ZLinkHttpClient" >&2
  exit 1
fi

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
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

reserve_ports() {
  local count="${1:-5}"
  python3 - "${count}" <<'PY'
import socket
import sys
count = int(sys.argv[1])
sockets = []
ports = []
try:
    for _ in range(count):
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
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_health() {
  local name="$1"
  local endpoint="$2"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if python3 - "${endpoint}/health" <<'PY'
import sys
import urllib.request

try:
    with urllib.request.urlopen(sys.argv[1], timeout=0.1) as response:
        sys.exit(0 if 200 <= response.status < 300 else 1)
except Exception:
    sys.exit(1)
PY
    then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} health at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon --no-parallel --max-workers=1 "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/registration-codec-client/bin/registration-codec-client"
}

main_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Main/install/registration-codec-main/bin/registration-codec-main"
}

invalid_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-InvalidDuplicate/install/registration-codec-invalid-duplicate/bin/registration-codec-invalid-duplicate"
}

json_only_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-JsonOnlyPeer/install/registration-codec-json-only-peer/bin/registration-codec-json-only-peer"
}

requester_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-CodecRequester/install/registration-codec-requester/bin/registration-codec-requester"
}

read -r SERVER_PORT HTTP_PORT INVALID_PORT MISMATCH_PORT MISMATCH_HTTP_PORT REQUESTER_HTTP_PORT <<<"$(reserve_ports 6)"
SERVER_ENDPOINT="$(tcp "${SERVER_PORT}")"
HTTP_ENDPOINT="$(http "${HTTP_PORT}")"
INVALID_ENDPOINT="$(tcp "${INVALID_PORT}")"
MISMATCH_ENDPOINT="$(tcp "${MISMATCH_PORT}")"
MISMATCH_HTTP_ENDPOINT="$(http "${MISMATCH_HTTP_PORT}")"
REQUESTER_HTTP_ENDPOINT="$(http "${REQUESTER_HTTP_PORT}")"

run_main_scenarios=false
run_mismatch_scenario=false
case "${SCENARIO}" in
  all)
    run_main_scenarios=true
    run_mismatch_scenario=true
    ;;
  RC-A6)
    ;;
  RC-B5)
    run_mismatch_scenario=true
    ;;
  RC-A1|RC-A2|RC-A3|RC-A4|RC-A5|RC-B1|RC-B2|RC-B3|RC-B4)
    run_main_scenarios=true
    ;;
  *)
    echo "Unknown RegistrationCodec scenario: ${SCENARIO}" >&2
    exit 1
    ;;
esac

rm -rf "${ZLINK_JAVA_E2E_BUILD_DIR}"
gradle_run installDist

if [[ "${run_main_scenarios}" == "true" ]]; then
  ZLINK_JAVA_E2E_SERVER_ENDPOINT="${SERVER_ENDPOINT}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${HTTP_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(main_bin)" >"${log_dir}/server.stdout.log" 2>"${log_dir}/server.stderr.log" &
  pids+=("$!")
  wait_port server "${SERVER_ENDPOINT}"
  wait_health server "${HTTP_ENDPOINT}"
  sleep "${ROUTE_SETTLE_SECONDS}"
fi

if [[ "${run_mismatch_scenario}" == "true" ]]; then
  ZLINK_JAVA_E2E_SERVER_ENDPOINT="${MISMATCH_ENDPOINT}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${MISMATCH_HTTP_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(json_only_bin)" >"${log_dir}/mismatch-server.stdout.log" 2>"${log_dir}/mismatch-server.stderr.log" &
  pids+=("$!")
  wait_port mismatch-server "${MISMATCH_ENDPOINT}"
  wait_health mismatch-server "${MISMATCH_HTTP_ENDPOINT}"
  sleep "${ROUTE_SETTLE_SECONDS}"

  ZLINK_JAVA_E2E_SERVER_ENDPOINT="${MISMATCH_ENDPOINT}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${REQUESTER_HTTP_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(requester_bin)" >"${log_dir}/codec-requester.stdout.log" 2>"${log_dir}/codec-requester.stderr.log" &
  pids+=("$!")
  wait_health codec-requester "${REQUESTER_HTTP_ENDPOINT}"
  sleep "${ROUTE_SETTLE_SECONDS}"
fi

ZLINK_JAVA_E2E_SERVER_ENDPOINT="${SERVER_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${HTTP_ENDPOINT}" \
ZLINK_JAVA_E2E_CODEC_REQUESTER_HTTP_ENDPOINT="${REQUESTER_HTTP_ENDPOINT}" \
ZLINK_JAVA_E2E_INVALID_SERVER_ENDPOINT="${INVALID_ENDPOINT}" \
ZLINK_JAVA_E2E_SCENARIO="${SCENARIO}" \
ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

cat "${log_dir}/client.stdout.log"
if [[ "${run_main_scenarios}" == "true" ]]; then
  python3 - "${HTTP_ENDPOINT}/evidence" >"${log_dir}/server-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY

  grep -Rq "message flow" "${log_dir}"/*-flow.log
fi
if [[ "${SCENARIO}" == "all" ]]; then
  grep -q "EchoAuto" "${log_dir}/server-evidence.json"
  grep -q "ProtobufEcho" "${log_dir}/server-evidence.json"
  grep -q "MsgpackEcho" "${log_dir}/server-evidence.json"
  grep -q "scenario RC-A4 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario RC-A6 passed" "${log_dir}/client.stdout.log"
  grep -q "scenario RC-B5 passed" "${log_dir}/client.stdout.log"
else
  grep -q "scenario ${SCENARIO} passed" "${log_dir}/client.stdout.log"
fi
