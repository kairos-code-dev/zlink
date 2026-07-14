#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/e2e-redis-common.sh"

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
redis_container=""
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"
SCENARIO="${1:-all}"
echo "start_order=${E2E_START_ORDER}"
echo "scenario=${SCENARIO}"

if [[ ! -f "Server/Session/build.gradle.kts" ]]; then
  echo "ToActorMessaging requires the session gateway role" >&2
  exit 1
fi

if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi

export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:toactor:${run_id}}"
export ZLINK_JAVA_E2E_LOG_DIR="${log_dir}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_SECONDS=5
if [[ "${LOCAL_READINESS_TIMEOUT_SECONDS:-}" != 3 \
   || "${LOCAL_READINESS_ATTEMPTS:-}" != 30 \
   || "${ROUTE_SETTLE_SECONDS:-}" != 5 ]]; then
  echo "ToActorMessaging must use 3s readiness and 5s route settle limits" >&2
  exit 1
fi

reserve_ports() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(10):
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

wait_tcp() {
  local host="$1"
  local port="$2"
  local name="$3"
  if python3 - "$host" "$port" "${LOCAL_READINESS_TIMEOUT_SECONDS}" "${LOCAL_READINESS_POLL_SECONDS}" <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
deadline = time.monotonic() + float(sys.argv[3])
while time.monotonic() < deadline:
    try:
        with socket.create_connection((host, port), timeout=1):
            sys.exit(0)
    except OSError:
        time.sleep(float(sys.argv[4]))
sys.exit(1)
PY
  then
    return 0
  fi
  echo "Timed out waiting for ${name} at ${host}:${port}" >&2
  return 1
}

zlink_redis_start_scoped_assign redis_container redis_port \
  "zlink-redis-java-e2e" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}" "127.0.0.1::6379"
redis_endpoint="127.0.0.1:${redis_port}"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${redis_endpoint}"
redis_host="${redis_endpoint%:*}"
redis_port="${redis_endpoint##*:}"
wait_tcp "${redis_host}" "${redis_port}" redis

read -r actor_http caller_http session_a_http session_b_http \
  actor_spot caller_spot session_a_spot session_b_spot \
  session_a_stream session_b_stream < <(reserve_ports)
export ZLINK_JAVA_E2E_ACTOR_HTTP="http://127.0.0.1:${actor_http}"
export ZLINK_JAVA_E2E_CALLER_HTTP="http://127.0.0.1:${caller_http}"
export ZLINK_JAVA_E2E_SESSION_A_HTTP="http://127.0.0.1:${session_a_http}"
export ZLINK_JAVA_E2E_SESSION_B_HTTP="http://127.0.0.1:${session_b_http}"
export ZLINK_JAVA_E2E_ACTOR_SPOT="tcp://127.0.0.1:${actor_spot}"
export ZLINK_JAVA_E2E_CALLER_SPOT="tcp://127.0.0.1:${caller_spot}"
export ZLINK_JAVA_E2E_SESSION_A_STREAM="tcp://127.0.0.1:${session_a_stream}"
export ZLINK_JAVA_E2E_SESSION_B_STREAM="tcp://127.0.0.1:${session_b_stream}"
export ZLINK_JAVA_E2E_ACTOR_RID="actor-a"
export ZLINK_JAVA_E2E_CALLER_RID="${ZLINK_JAVA_E2E_CALLER_RID:-aaa-caller}"
echo "session_a_spot=tcp://127.0.0.1:${session_a_spot} session_a_stream=${ZLINK_JAVA_E2E_SESSION_A_STREAM}"
echo "session_b_spot=tcp://127.0.0.1:${session_b_spot} session_b_stream=${ZLINK_JAVA_E2E_SESSION_B_STREAM}"

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
  for _ in $(seq 1 50); do
    local alive=0
    for pid in "${pids[@]}"; do
      if kill -0 "${pid}" >/dev/null 2>&1; then
        alive=1
        break
      fi
    done
    [[ "${alive}" == "0" ]] && break
    sleep 0.1
  done
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill -9 "${pids[$i]}" >/dev/null 2>&1 || true
  done
  if [[ -n "${redis_container}" ]]; then
    docker rm -fv "${redis_container}" >/dev/null 2>&1 || true
  fi
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

wait_http() {
  local endpoint="$1"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if python3 - "${endpoint}/health" >/dev/null 2>&1 <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=1) as response:
    response.read()
PY
    then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${endpoint}" >&2
  return 1
}

ordered_roles() {
  python3 - "${E2E_START_ORDER}" "$@" <<'PY'
import random
import sys

mode = sys.argv[1]
roles = sys.argv[2:]
if mode in ("", "forward"):
    pass
elif mode == "reverse":
    roles.reverse()
elif mode.startswith("shuffle:"):
    seed_text = mode.split(":", 1)[1]
    if seed_text == "":
        raise SystemExit("E2E_START_ORDER shuffle requires a seed")
    random.Random(int(seed_text)).shuffle(roles)
else:
    raise SystemExit(f"unsupported E2E_START_ORDER={mode!r}")
for role in roles:
    print(role)
PY
}

start_role() {
  case "$1" in
    actor)
      ./Server/Actor/build/install/to-actor-actor/bin/to-actor-actor >"${log_dir}/actor.log" 2>&1 &
      pids+=("$!")
      ;;
    caller)
      ./Server/Caller/build/install/to-actor-caller/bin/to-actor-caller >"${log_dir}/caller.log" 2>&1 &
      pids+=("$!")
      ;;
    session-a)
      ZLINK_JAVA_E2E_SESSION_RID="session-a" \
      ZLINK_JAVA_E2E_SESSION_HTTP="${ZLINK_JAVA_E2E_SESSION_A_HTTP}" \
      ZLINK_JAVA_E2E_SESSION_SPOT="tcp://127.0.0.1:${session_a_spot}" \
      ZLINK_JAVA_E2E_SESSION_STREAM="${ZLINK_JAVA_E2E_SESSION_A_STREAM}" \
        ./Server/Session/build/install/to-actor-session/bin/to-actor-session \
        >"${log_dir}/session-a.log" 2>&1 &
      pids+=("$!")
      ;;
    session-b)
      ZLINK_JAVA_E2E_SESSION_RID="session-b" \
      ZLINK_JAVA_E2E_SESSION_HTTP="${ZLINK_JAVA_E2E_SESSION_B_HTTP}" \
      ZLINK_JAVA_E2E_SESSION_SPOT="tcp://127.0.0.1:${session_b_spot}" \
      ZLINK_JAVA_E2E_SESSION_STREAM="${ZLINK_JAVA_E2E_SESSION_B_STREAM}" \
        ./Server/Session/build/install/to-actor-session/bin/to-actor-session \
        >"${log_dir}/session-b.log" 2>&1 &
      pids+=("$!")
      ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

wait_role() {
  case "$1" in
    actor) wait_http "${ZLINK_JAVA_E2E_ACTOR_HTTP}" ;;
    caller) wait_http "${ZLINK_JAVA_E2E_CALLER_HTTP}" ;;
    session-a) wait_http "${ZLINK_JAVA_E2E_SESSION_A_HTTP}" ;;
    session-b) wait_http "${ZLINK_JAVA_E2E_SESSION_B_HTTP}" ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

wait_log() {
  local log="$1"
  local pattern="$2"
  local description="$3"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if [[ -f "${log}" ]] && grep -q "${pattern}" "${log}"; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${description}" >&2
  return 1
}

wait_role_ready() {
  case "$1" in
    actor) wait_log "${log_dir}/actor.log" "\\[boot\\] role=actor step=baselineActors done" "actor baseline readiness" ;;
    caller) wait_log "${log_dir}/caller.log" "\\[boot\\] role=caller step=main run done" "caller application readiness" ;;
    session-a) wait_log "${log_dir}/session-a.log" "\[boot\] role=session rid=session-a step=main run done" "session-a application readiness" ;;
    session-b) wait_log "${log_dir}/session-b.log" "\[boot\] role=session rid=session-b step=main run done" "session-b application readiness" ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

../../gradlew --no-daemon --no-parallel --max-workers=1 --gradle-user-home "${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/toactor-gradle}" -p . installDist

SERVER_ROLES=(actor caller session-a session-b)
mapfile -t ORDERED_SERVER_ROLES < <(ordered_roles "${SERVER_ROLES[@]}")
for role in "${ORDERED_SERVER_ROLES[@]}"; do
  start_role "$role"
done
for role in "${SERVER_ROLES[@]}"; do
  wait_role "$role"
done
for role in "${SERVER_ROLES[@]}"; do
  wait_role_ready "$role"
done
sleep "${ROUTE_SETTLE_SECONDS}"

./Client/build/install/to-actor-client/bin/to-actor-client "${SCENARIO}" \
  > >(tee "${log_dir}/client.log") 2>"${log_dir}/client.stderr.log"
