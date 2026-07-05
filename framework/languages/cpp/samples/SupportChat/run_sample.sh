#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export SUPPORTCHAT_LOG_DIR="${SUPPORTCHAT_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$SUPPORTCHAT_LOG_DIR"
rm -f "$SUPPORTCHAT_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_supportchat_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_supportchat_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PIDS=()
RUN_DIR="${SUPPORTCHAT_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="$RUN_DIR/logs"
REDIS_CONTAINER_NAME=""
mkdir -p "$LOG_DIR"
cleanup() {
  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
    fi
    wait "${pid}" 2>/dev/null || true
  done
  if [[ -n "$REDIS_CONTAINER_NAME" ]]; then
    docker rm -f "$REDIS_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
  if [[ "${SUPPORTCHAT_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    [[ -z "${SUPPORTCHAT_RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
  fi
}
trap cleanup EXIT

PORT_ALLOCATION_OUTPUT="$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(8):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    ports = [sock.getsockname()[1] for sock in sockets]
    print(
        f"{ports[0]} "
        f"tcp://127.0.0.1:{ports[1]} "
        f"tcp://127.0.0.1:{ports[2]} "
        f"tcp://127.0.0.1:{ports[3]} "
        f"tcp://127.0.0.1:{ports[4]} "
        f"tcp://127.0.0.1:{ports[5]} "
        f"tcp://127.0.0.1:{ports[6]} "
        f"tcp://127.0.0.1:{ports[7]}"
    )
except OSError as error:
    print(f"SOCKETLESS {error.errno}:{error.strerror}")
finally:
    for sock in sockets:
        sock.close()
PY
)"
if [[ "$PORT_ALLOCATION_OUTPUT" == SOCKETLESS* ]]; then
  echo "SupportChat runner using socketless endpoints: ${PORT_ALLOCATION_OUTPUT#SOCKETLESS }" >&2
  SUPPORTCHAT_REDIS_PORT=""
  SUPPORTCHAT_API_ROUTE="socketless://supportchat-api"
  SUPPORTCHAT_SUPPORT_ROUTE="socketless://supportchat-support"
  SUPPORTCHAT_SUPPORT_SPOT_ROUTER="socketless://supportchat-support-spot-router"
  SUPPORTCHAT_SUPPORT_SPOT="socketless://supportchat-support-spot"
  SUPPORTCHAT_SESSION_STREAM="socketless://supportchat-session-stream"
  SUPPORTCHAT_SESSION_SPOT_ROUTER="socketless://supportchat-session-spot-router"
  SUPPORTCHAT_SESSION_SPOT="socketless://supportchat-session-spot"
else
  read -r SUPPORTCHAT_REDIS_PORT SUPPORTCHAT_API_ROUTE SUPPORTCHAT_SUPPORT_ROUTE SUPPORTCHAT_SUPPORT_SPOT_ROUTER SUPPORTCHAT_SUPPORT_SPOT SUPPORTCHAT_SESSION_STREAM SUPPORTCHAT_SESSION_SPOT_ROUTER SUPPORTCHAT_SESSION_SPOT <<<"$PORT_ALLOCATION_OUTPUT"
  if [[ -z "$SUPPORTCHAT_REDIS_PORT" || -z "$SUPPORTCHAT_SESSION_SPOT" ]]; then
    echo "Failed to allocate local TCP ports for the SupportChat sample." >&2
    echo "This environment may block local socket creation." >&2
    exit 1
  fi
fi
if [[ -z "${SUPPORTCHAT_REDIS_ENDPOINT:-}" ]]; then
  if [[ -z "$SUPPORTCHAT_REDIS_PORT" ]]; then
    export SUPPORTCHAT_REDIS_ENDPOINT="socketless://supportchat-redis"
  else
    REDIS_CONTAINER_NAME="zlink-cpp-supportchat-sample-redis-$$"
    docker run -d --rm --name "$REDIS_CONTAINER_NAME" -p "127.0.0.1:${SUPPORTCHAT_REDIS_PORT}:6379" redis:7-alpine >/dev/null
    export SUPPORTCHAT_REDIS_ENDPOINT="tcp://127.0.0.1:${SUPPORTCHAT_REDIS_PORT}"
  fi
fi
export SUPPORTCHAT_REDIS_KEY_PREFIX="${SUPPORTCHAT_REDIS_KEY_PREFIX:-supportchat:$$:}"
export SUPPORTCHAT_API_ROUTE
export SUPPORTCHAT_SUPPORT_ROUTE
export SUPPORTCHAT_SUPPORT_SPOT_ROUTER
export SUPPORTCHAT_SUPPORT_SPOT
export SUPPORTCHAT_SESSION_STREAM
export SUPPORTCHAT_SESSION_SPOT_ROUTER
export SUPPORTCHAT_SESSION_SPOT

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local label="$1"
  local port="$2"
  for _ in $(seq 1 150); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} on ${port}" >&2
  return 1
}

if [[ "$SUPPORTCHAT_REDIS_ENDPOINT" == tcp://* || "$SUPPORTCHAT_REDIS_ENDPOINT" == 127.0.0.1:* ]]; then
  wait_port redis "$(port_of "$SUPPORTCHAT_REDIS_ENDPOINT")"
fi

start_role() {
  local name="$1"
  shift
  stdbuf -oL -eL "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

dump_logs() {
  for log in "$LOG_DIR"/*.log "$SUPPORTCHAT_LOG_DIR"/flow-*.log; do
    if [[ -f "$log" ]]; then
      echo "===== ${log}" >&2
      cat "$log" >&2
    fi
  done
}

cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_supportchat_api \
  sample_cpp_framework_supportchat_session \
  sample_cpp_framework_supportchat_support \
  sample_cpp_framework_supportchat_probe \
  sample_cpp_framework_supportchat_client >/dev/null

start_role api "$BIN_DIR/sample_cpp_framework_supportchat_api"
start_role session "$BIN_DIR/sample_cpp_framework_supportchat_session"
start_role support "$BIN_DIR/sample_cpp_framework_supportchat_support"

sleep "${SUPPORTCHAT_CPP_STARTUP_SETTLE_SECONDS:-1}"

"$BIN_DIR/sample_cpp_framework_supportchat_probe" >"$LOG_DIR/probe.log" 2>&1 || {
  dump_logs
  exit 1
}
grep -q "topology=ready" "$LOG_DIR/probe.log"

"$BIN_DIR/sample_cpp_framework_supportchat_client" >"$LOG_DIR/client.log" 2>&1 || {
  dump_logs
  exit 1
}

grep -q "supportchat authentication=verified" "$LOG_DIR/client.log"
grep -q "supportchat conversation-assignment=verified" "$LOG_DIR/client.log"
grep -q "supportchat bound-push=verified" "$LOG_DIR/client.log"
grep -q "supportchat reconnect=verified" "$LOG_DIR/client.log"
grep -q "supportchat idle-close=verified" "$LOG_DIR/client.log"
grep -q "supportchat=completed" "$LOG_DIR/client.log"
grep -Rq "message flow" "$SUPPORTCHAT_LOG_DIR"
grep -q "message flow" "$SUPPORTCHAT_LOG_DIR/flow-api.log"
grep -q "message flow" "$SUPPORTCHAT_LOG_DIR/flow-session.log"
grep -q "message flow" "$SUPPORTCHAT_LOG_DIR/flow-support.log"
grep -q "message flow" "$SUPPORTCHAT_LOG_DIR/flow-probe.log"

echo "PASS SupportChat.Cpp"
echo "supportchat sample result=passed"
