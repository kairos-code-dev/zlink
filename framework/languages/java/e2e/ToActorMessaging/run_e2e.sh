#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"

if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi

export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT:-${ZLINK_REDIS_LOCATION_ENDPOINT:-127.0.0.1:16379}}"
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:toactor:${run_id}}"
export ZLINK_JAVA_E2E_LOG_DIR="${log_dir}"

reserve_ports() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(4):
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

read -r actor_http caller_http actor_spot caller_spot < <(reserve_ports)
export ZLINK_JAVA_E2E_ACTOR_HTTP="http://127.0.0.1:${actor_http}"
export ZLINK_JAVA_E2E_CALLER_HTTP="http://127.0.0.1:${caller_http}"
export ZLINK_JAVA_E2E_ACTOR_SPOT="tcp://127.0.0.1:${actor_spot}"
export ZLINK_JAVA_E2E_CALLER_SPOT="tcp://127.0.0.1:${caller_spot}"
export ZLINK_JAVA_E2E_ACTOR_RID="actor-a"
export ZLINK_JAVA_E2E_CALLER_RID="caller"

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

cleanup() {
  local status="$?"
  set +e
  print_logs "${status}"
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill "${pids[$i]}" >/dev/null 2>&1 || true
  done
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

wait_http() {
  local endpoint="$1"
  for _ in $(seq 1 300); do
    if python3 - "${endpoint}/health" >/dev/null 2>&1 <<'PY'
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
  echo "Timed out waiting for ${endpoint}" >&2
  return 1
}

../../gradlew --no-daemon --gradle-user-home "${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/toactor-gradle}" -p . installDist

./Server/Actor/build/install/to-actor-actor/bin/to-actor-actor >"${log_dir}/actor.log" 2>&1 &
pids+=("$!")
wait_http "${ZLINK_JAVA_E2E_ACTOR_HTTP}"

./Server/Caller/build/install/to-actor-caller/bin/to-actor-caller >"${log_dir}/caller.log" 2>&1 &
pids+=("$!")
wait_http "${ZLINK_JAVA_E2E_CALLER_HTTP}"

./Client/build/install/to-actor-client/bin/to-actor-client | tee "${log_dir}/client.log"
