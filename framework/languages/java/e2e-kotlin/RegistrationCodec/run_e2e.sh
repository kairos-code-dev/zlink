#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.kotlin\.registrationcodec\.ProgramKt'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/RegistrationCodec}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/RegistrationCodec-gradle-cache}"

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
  local count="${1:-3}"
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

tcp() { echo "tcp://127.0.0.1:$1"; }
http() { echo "http://127.0.0.1:$1"; }
port_of() { echo "${1##*:}"; }

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
  ../../gradlew --project-cache-dir "${ZLINK_KOTLIN_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

bin_path() {
  local path="$1"
  local app="$2"
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/${path}/install/${app}/bin/${app}"
}

CLIENT_BIN="$(bin_path Client registration-codec-kotlin-client)"
MAIN_BIN="$(bin_path Server-Main registration-codec-kotlin-main)"
CODEC_REQUESTER_BIN="$(bin_path Server-CodecRequester registration-codec-kotlin-codec-requester)"
JSON_ONLY_BIN="$(bin_path Server-JsonOnlyPeer registration-codec-kotlin-json-only-peer)"
INVALID_BIN="$(bin_path Server-InvalidDuplicate registration-codec-kotlin-invalid-duplicate)"

read -r SERVER_PORT HTTP_PORT INVALID_PORT MISMATCH_PORT MISMATCH_HTTP_PORT REQUESTER_HTTP_PORT <<<"$(reserve_ports 6)"
SERVER_ENDPOINT="$(tcp "${SERVER_PORT}")"
HTTP_ENDPOINT="$(http "${HTTP_PORT}")"
INVALID_ENDPOINT="$(tcp "${INVALID_PORT}")"
MISMATCH_ENDPOINT="$(tcp "${MISMATCH_PORT}")"
MISMATCH_HTTP_ENDPOINT="$(http "${MISMATCH_HTTP_PORT}")"
REQUESTER_HTTP_ENDPOINT="$(http "${REQUESTER_HTTP_PORT}")"

gradle_run installDist

set +e
"${INVALID_BIN}" \
  --server-endpoint "${INVALID_ENDPOINT}" \
  --log-dir "${log_dir}" \
  >"${log_dir}/invalid-server.stdout.log" 2>"${log_dir}/invalid-server.stderr.log"
invalid_status="$?"
set -e
if [[ "${invalid_status}" == "0" ]]; then
  echo "invalid registration server unexpectedly started" >&2
  exit 1
fi
cat "${log_dir}/invalid-server.stdout.log" "${log_dir}/invalid-server.stderr.log" \
  | grep -Eq "duplicate|Duplicate|registration|packet"
echo "scenario RC-A6 passed"

"${MAIN_BIN}" \
  --server-endpoint "${SERVER_ENDPOINT}" \
  --http-endpoint "${HTTP_ENDPOINT}" \
  --log-dir "${log_dir}" \
  --codec-mode all \
  >"${log_dir}/server.stdout.log" 2>"${log_dir}/server.stderr.log" &
pids+=("$!")
wait_port server "${SERVER_ENDPOINT}"
wait_port evidence "${HTTP_ENDPOINT}"
sleep 1

"${CLIENT_BIN}" \
  --server-endpoint "${SERVER_ENDPOINT}" \
  --http-endpoint "${HTTP_ENDPOINT}" \
  --log-dir "${log_dir}" \
  >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

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

stop_current() {
  set +e
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill "${pids[$i]}" >/dev/null 2>&1 || true
  done
  wait >/dev/null 2>&1 || true
  pids=()
  set -e
}

stop_current

"${JSON_ONLY_BIN}" \
  --server-endpoint "${MISMATCH_ENDPOINT}" \
  --http-endpoint "${MISMATCH_HTTP_ENDPOINT}" \
  --log-dir "${log_dir}" \
  --codec-mode json-only \
  >"${log_dir}/mismatch-server.stdout.log" 2>"${log_dir}/mismatch-server.stderr.log" &
pids+=("$!")
wait_port mismatch-server "${MISMATCH_ENDPOINT}"
wait_port mismatch-evidence "${MISMATCH_HTTP_ENDPOINT}"
sleep 1

"${CODEC_REQUESTER_BIN}" \
  --server-endpoint "${MISMATCH_ENDPOINT}" \
  --http-endpoint "${REQUESTER_HTTP_ENDPOINT}" \
  --log-dir "${log_dir}" \
  >"${log_dir}/codec-requester.stdout.log" 2>"${log_dir}/codec-requester.stderr.log" &
pids+=("$!")
wait_port codec-requester "${REQUESTER_HTTP_ENDPOINT}"
sleep 1

python3 - "${REQUESTER_HTTP_ENDPOINT}" >"${log_dir}/codec-requester-result.json" <<'PY'
import json
import sys
import urllib.request

base = sys.argv[1]

def post(path):
    request = urllib.request.Request(base + path, data=b"", method="POST")
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))

json_reply = post("/codec/json/request")
if json_reply.get("value") != "echo:json-from-requester":
    raise SystemExit(f"json requester failed: {json_reply}")
protobuf_reply = post("/codec/protobuf/request")
if not protobuf_reply.get("error"):
    raise SystemExit(f"protobuf mismatch did not fail: {protobuf_reply}")
print(json.dumps({"json": json_reply, "protobuf": protobuf_reply}, sort_keys=True))
PY

cat "${log_dir}/codec-requester-result.json"
grep -q "scenario RC-A4 passed" "${log_dir}/client.stdout.log"
echo "scenario RC-B5 passed"
