#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-${ZLINK_CPP_BUILD_DIR:-$SCRIPT_DIR/../../build}}"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"
pids=()
REDIS_CONTAINER=""

mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"
echo "start_order=$E2E_START_ORDER"

export ZLINK_CPP_E2E_LOCATION_KEY_PREFIX="${ZLINK_CPP_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:toactor:$RUN_ID}"
export ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR"
export ZLINK_CPP_E2E_ACTOR_RID="${ZLINK_CPP_E2E_ACTOR_RID:-actor-a}"
export ZLINK_CPP_E2E_CALLER_RID="${ZLINK_CPP_E2E_CALLER_RID:-caller}"

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
    print(" ".join(str(port) for port in ports))
finally:
    for sock in sockets:
        sock.close()
PY
}

read -r actor_http caller_http actor_spot caller_spot actor_pub caller_pub < <(reserve_ports)
export ZLINK_CPP_E2E_ACTOR_HTTP="http://127.0.0.1:${actor_http}"
export ZLINK_CPP_E2E_CALLER_HTTP="http://127.0.0.1:${caller_http}"
export ZLINK_CPP_E2E_ACTOR_SPOT="tcp://127.0.0.1:${actor_spot}"
export ZLINK_CPP_E2E_CALLER_SPOT="tcp://127.0.0.1:${caller_spot}"
export ZLINK_CPP_E2E_ACTOR_PUBSUB="tcp://127.0.0.1:${actor_pub}"
export ZLINK_CPP_E2E_CALLER_PUBSUB="tcp://127.0.0.1:${caller_pub}"

wait_tcp() {
  local host="$1"
  local port="$2"
  local name="$3"
  if python3 - "$host" "$port" <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
deadline = time.monotonic() + 30
while time.monotonic() < deadline:
    try:
        with socket.create_connection((host, port), timeout=1):
            sys.exit(0)
    except OSError:
        time.sleep(0.2)
sys.exit(1)
PY
  then
    return 0
  fi
  echo "Timed out waiting for $name at $host:$port" >&2
  return 1
}

if [[ -n "${ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT:-}" ]]; then
  REDIS_ENDPOINT="$ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT"
  echo "redis endpoint=$REDIS_ENDPOINT (external)"
elif [[ -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  REDIS_ENDPOINT="$ZLINK_REDIS_E2E_ENDPOINT"
  echo "redis endpoint=$REDIS_ENDPOINT (external)"
else
  read -r redis_port < <(python3 - <<'PY'
import socket
sock = socket.socket()
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
)
  REDIS_CONTAINER="zlink-e2e-toactor-cpp-$$"
  docker run -d --rm --name "$REDIS_CONTAINER" -p "127.0.0.1:${redis_port}:6379" redis:7-alpine >/dev/null
  REDIS_ENDPOINT="127.0.0.1:${redis_port}"
  echo "redis endpoint=$REDIS_ENDPOINT (container $REDIS_CONTAINER)"
fi
export ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT="$REDIS_ENDPOINT"
REDIS_HOST="${REDIS_ENDPOINT%:*}"
REDIS_TCP_PORT="${REDIS_ENDPOINT##*:}"
wait_tcp "$REDIS_HOST" "$REDIS_TCP_PORT" redis
echo "redis key prefix=$ZLINK_CPP_E2E_LOCATION_KEY_PREFIX"

print_logs() {
  local status="$1"
  if [[ "$status" == "0" ]]; then
    return
  fi
  for log in "$LOG_DIR"/*.log; do
    [[ -f "$log" ]] || continue
    echo "===== $log =====" >&2
    tail -n 200 "$log" >&2 || true
  done
}

cleanup() {
  local status="$?"
  set +e
  print_logs "$status"
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill "${pids[$i]}" >/dev/null 2>&1 || true
  done
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  wait >/dev/null 2>&1 || true
  exit "$status"
}
trap cleanup EXIT

ordered_roles() {
  python3 - "$E2E_START_ORDER" "$@" <<'PY'
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
  echo "Timed out waiting for $endpoint" >&2
  return 1
}

start_role() {
  case "$1" in
    actor)
      "$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_actor" >"$LOG_DIR/actor.stdout.log" 2>"$LOG_DIR/actor.stderr.log" &
      pids+=("$!")
      ;;
    caller)
      "$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_caller" >"$LOG_DIR/caller.stdout.log" 2>"$LOG_DIR/caller.stderr.log" &
      pids+=("$!")
      ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

wait_role() {
  case "$1" in
    actor) wait_http "$ZLINK_CPP_E2E_ACTOR_HTTP" ;;
    caller) wait_http "$ZLINK_CPP_E2E_CALLER_HTTP" ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

mapfile -t ORDERED_SERVER_ROLES < <(ordered_roles actor caller)
for role in "${ORDERED_SERVER_ROLES[@]}"; do
  start_role "$role"
  wait_role "$role"
done

"$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_client" > >(tee "$LOG_DIR/client.log") 2>"$LOG_DIR/client.stderr.log"
