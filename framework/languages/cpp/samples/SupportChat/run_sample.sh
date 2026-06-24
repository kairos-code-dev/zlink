#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
export SUPPORTCHAT_LOG_DIR="${SUPPORTCHAT_LOG_DIR:-$SCRIPT_DIR/logs}"
mkdir -p "$SUPPORTCHAT_LOG_DIR"
rm -f "$SUPPORTCHAT_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"

if [[ ! -x "$BIN_DIR/sample_cpp_framework_supportchat_registry" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_supportchat_registry" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

REGISTRY_BIN="$BIN_DIR/sample_cpp_framework_supportchat_registry"
SUPPORT_BIN="$BIN_DIR/sample_cpp_framework_supportchat_support"
API_BIN="$BIN_DIR/sample_cpp_framework_supportchat_api"
SESSION_BIN="$BIN_DIR/sample_cpp_framework_supportchat_session"
CLIENT_BIN="$BIN_DIR/sample_cpp_framework_supportchat_client"

for binary in "$REGISTRY_BIN" "$SUPPORT_BIN" "$API_BIN" "$SESSION_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

if [[ -n "${SUPPORTCHAT_CPP_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 13); do
    PORTS+=("$((SUPPORTCHAT_CPP_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 13:
        port = random.randint(48000, 60999)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"
fi

REGISTRY_PUB_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
REGISTRY_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"
API_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[2]}"
SUPPORT_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[3]}"
SESSION_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
SESSION_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
SESSION_ACTOR_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
SUPPORT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"
SUPPORT_ACTOR_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[8]}"
SUPPORT_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
CONVERSATION_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[10]}"
CONVERSATION_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[11]}"
STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[12]}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 200); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR" "$SUPPORTCHAT_LOG_DIR"
PIDS=()
cleanup_done=false

cleanup() {
  set +e
  if [[ "$cleanup_done" == true ]]; then
    return
  fi
  cleanup_done=true
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    kill "${PIDS[$i]}" 2>/dev/null || true
  done
  for _ in $(seq 1 30); do
    local any_alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        any_alive=1
        break
      fi
    done
    if [[ "$any_alive" == "0" ]]; then
      break
    fi
    sleep 0.1
  done
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    kill -9 "${PIDS[$i]}" 2>/dev/null || true
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
  if [[ "${SUPPORTCHAT_CPP_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    rm -rf "$RUN_DIR"
  fi
}
trap cleanup EXIT

topology_args=(
  "--sample.topology.registryPubEndpoint=$REGISTRY_PUB_ENDPOINT"
  "--sample.topology.registryRouterEndpoint=$REGISTRY_ROUTER_ENDPOINT"
  "--sample.topology.apiChannelEndpoint=$API_CHANNEL_ENDPOINT"
  "--sample.topology.supportChannelEndpoint=$SUPPORT_CHANNEL_ENDPOINT"
  "--sample.topology.sessionSpotEndpoint=$SESSION_SPOT_ENDPOINT"
  "--sample.topology.sessionRouterEndpoint=$SESSION_ROUTER_ENDPOINT"
  "--sample.topology.sessionActorRouteEndpoint=$SESSION_ACTOR_ROUTE_ENDPOINT"
  "--sample.topology.supportRouterEndpoint=$SUPPORT_ROUTER_ENDPOINT"
  "--sample.topology.supportActorRouteEndpoint=$SUPPORT_ACTOR_ROUTE_ENDPOINT"
  "--sample.topology.supportSpotEndpoint=$SUPPORT_SPOT_ENDPOINT"
  "--sample.topology.conversationSpotRouterEndpoint=$CONVERSATION_SPOT_ROUTER_ENDPOINT"
  "--sample.topology.conversationSpotEndpoint=$CONVERSATION_SPOT_ENDPOINT"
  "--sample.topology.streamEndpoint=$STREAM_ENDPOINT"
)

start_server() {
  local name="$1"
  local binary="$2"
  shift 2
  "$binary" --sample.host.keepRunning true "${topology_args[@]}" "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_server registry "$REGISTRY_BIN"
wait_port registry-router "$REGISTRY_ROUTER_ENDPOINT"

start_server support "$SUPPORT_BIN"
wait_port support-channel "$SUPPORT_CHANNEL_ENDPOINT"
wait_port support-router "$SUPPORT_ROUTER_ENDPOINT"
wait_port support-actor-route "$SUPPORT_ACTOR_ROUTE_ENDPOINT"
wait_port support-spot "$SUPPORT_SPOT_ENDPOINT"

start_server api "$API_BIN"
wait_port api-channel "$API_CHANNEL_ENDPOINT"

start_server session "$SESSION_BIN"
wait_port session-router "$SESSION_ROUTER_ENDPOINT"
wait_port session-spot "$SESSION_SPOT_ENDPOINT"
wait_port session-actor-route "$SESSION_ACTOR_ROUTE_ENDPOINT"
wait_port session-stream "$STREAM_ENDPOINT"

sleep "${SUPPORTCHAT_CPP_STARTUP_SETTLE_SECONDS:-1}"

"$CLIENT_BIN" --stream-endpoint "$STREAM_ENDPOINT" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/registry.log" >&2
  cat "$LOG_DIR/support.log" >&2
  cat "$LOG_DIR/api.log" >&2
  cat "$LOG_DIR/session.log" >&2
  exit 1
}

grep -q "supportchat=completed" "$LOG_DIR/client.log"
grep -Rq "message flow" "$SUPPORTCHAT_LOG_DIR"

cleanup
trap - EXIT

echo "PASS SupportChat.Cpp"
echo "supportchat full client/server self-check completed"
