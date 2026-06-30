#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Message-flow logs land in the sample's own logs/ folder (git-ignored).
export BINGO_LOG_DIR="${BINGO_LOG_DIR:-$SCRIPT_DIR/logs}"
mkdir -p "$BINGO_LOG_DIR"
rm -f "$BINGO_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"

if [[ ! -x "$BIN_DIR/sample_cpp_framework_bingo_registry" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_bingo_registry" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

REGISTRY_BIN="$BIN_DIR/sample_cpp_framework_bingo_registry"
API_BIN="$BIN_DIR/sample_cpp_framework_bingo_api"
PLAY_BIN="$BIN_DIR/sample_cpp_framework_bingo_play"
SESSION_BIN="$BIN_DIR/sample_cpp_framework_bingo_session"
CLIENT_BIN="$BIN_DIR/sample_cpp_framework_bingo_client"
CTEST_BIN="${CTEST_BIN:-ctest}"

for binary in "$REGISTRY_BIN" "$API_BIN" "$PLAY_BIN" "$SESSION_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

"$CTEST_BIN" --test-dir "$BUILD_DIR" \
  -R 'test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay' \
  --output-on-failure

if [[ -n "${BINGO_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 22); do
    PORTS+=("$((BINGO_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    blocked = set()
    while len(sockets) < 22:
        port = random.randint(48000, 60999)
        if port in chosen or port in blocked or port + 1000 in chosen or port + 1000 in blocked:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
            ctrl.bind(("127.0.0.1", port + 1000))
        except OSError:
            sock.close()
            ctrl.close()
            continue
        chosen.add(port)
        blocked.add(port + 1000)
        ctrl.close()
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"
fi

REGISTRY_PUB_ENDPOINT="${BINGO_REGISTRY_PUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[0]}}"
REGISTRY_ROUTER_ENDPOINT="${BINGO_REGISTRY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[1]}}"
API_A_CHANNEL_ENDPOINT="${BINGO_API_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
PLAY_A_CHANNEL_ENDPOINT="${BINGO_PLAY_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
SESSION_A_SPOT_ENDPOINT="${BINGO_SESSION_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
SESSION_A_ROUTER_ENDPOINT="${BINGO_SESSION_A_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
SESSION_B_SPOT_ENDPOINT="${BINGO_SESSION_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
SESSION_B_ROUTER_ENDPOINT="${BINGO_SESSION_B_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
PLAY_B_CHANNEL_ENDPOINT="${BINGO_PLAY_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
PLAY_A_SPOT_ENDPOINT="${BINGO_PLAY_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
PLAY_A_SPOT_ROUTER_ENDPOINT="${BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
SESSION_A_STREAM_ENDPOINT="${BINGO_SESSION_A_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
SESSION_B_STREAM_ENDPOINT="${BINGO_SESSION_B_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"
PLAY_B_SPOT_ENDPOINT="${BINGO_PLAY_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[13]}}"
PLAY_B_SPOT_ROUTER_ENDPOINT="${BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[14]}}"
API_B_CHANNEL_ENDPOINT="${BINGO_API_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[15]}}"
REDIS_PORT="${PORTS[16]}"
API_A_PLAY_ROUTE_ENDPOINT="${BINGO_API_A_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[17]}}"
API_B_PLAY_ROUTE_ENDPOINT="${BINGO_API_B_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[18]}}"
SESSION_A_PLAY_ROUTE_ENDPOINT="${BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[19]}}"
SESSION_B_PLAY_ROUTE_ENDPOINT="${BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[20]}}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 600); do
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
mkdir -p "$LOG_DIR"
PIDS=()
REDIS_CONTAINER=""
cleanup_done=false
BINGO_REDIS_KEY_PREFIX="${BINGO_REDIS_KEY_PREFIX:-bingo:cpp:${RANDOM}:$$:}"

cleanup() {
  set +e
  if [[ "$cleanup_done" == true ]]; then
    return
  fi
  cleanup_done=true
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    kill "$pid" 2>/dev/null || true
  done
  for _ in $(seq 1 20); do
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
    local pid="${PIDS[$i]}"
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ "${BINGO_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    rm -rf "$RUN_DIR"
  fi
}
trap cleanup EXIT

if [[ -n "${BINGO_REDIS_ENDPOINT:-}" ]]; then
  wait_port redis "tcp://${BINGO_REDIS_ENDPOINT}"
else
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required to run the Bingo sample when BINGO_REDIS_ENDPOINT is not set." >&2
    exit 1
  fi
  REDIS_CONTAINER="bingo-cpp-redis-${RANDOM}-$$"
  docker run -d --rm --name "$REDIS_CONTAINER" -p "127.0.0.1::6379" redis:7.2-alpine >/dev/null
  BINGO_REDIS_ENDPOINT="$(docker port "$REDIS_CONTAINER" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
  wait_port redis "tcp://${BINGO_REDIS_ENDPOINT}"
fi

topology_args=(
  "--sample.topology.registryPubEndpoint=$REGISTRY_PUB_ENDPOINT"
  "--sample.topology.registryRouterEndpoint=$REGISTRY_ROUTER_ENDPOINT"
  "--sample.topology.apiChannelEndpoint=$API_A_CHANNEL_ENDPOINT"
  "--sample.topology.apiAChannelEndpoint=$API_A_CHANNEL_ENDPOINT"
  "--sample.topology.apiBChannelEndpoint=$API_B_CHANNEL_ENDPOINT"
  "--sample.topology.playChannelEndpoint=$PLAY_A_CHANNEL_ENDPOINT"
  "--sample.topology.playAChannelEndpoint=$PLAY_A_CHANNEL_ENDPOINT"
  "--sample.topology.playBChannelEndpoint=$PLAY_B_CHANNEL_ENDPOINT"
  "--sample.topology.playARouteEndpoint=$API_A_PLAY_ROUTE_ENDPOINT"
  "--sample.topology.playBRouteEndpoint=$API_B_PLAY_ROUTE_ENDPOINT"
  "--sample.topology.sessionAPlayRouteEndpoint=$SESSION_A_PLAY_ROUTE_ENDPOINT"
  "--sample.topology.sessionBPlayRouteEndpoint=$SESSION_B_PLAY_ROUTE_ENDPOINT"
  "--sample.topology.playASpotEndpoint=$PLAY_A_SPOT_ENDPOINT"
  "--sample.topology.playBSpotEndpoint=$PLAY_B_SPOT_ENDPOINT"
  "--sample.topology.playASpotRouterEndpoint=$PLAY_A_SPOT_ROUTER_ENDPOINT"
  "--sample.topology.playBSpotRouterEndpoint=$PLAY_B_SPOT_ROUTER_ENDPOINT"
  "--sample.topology.sessionSpotEndpoint=$SESSION_A_SPOT_ENDPOINT"
  "--sample.topology.sessionRouterEndpoint=$SESSION_A_ROUTER_ENDPOINT"
  "--sample.topology.sessionAStreamEndpoint=$SESSION_A_STREAM_ENDPOINT"
  "--sample.topology.sessionBStreamEndpoint=$SESSION_B_STREAM_ENDPOINT"
  "--sample.topology.redisEndpoint=$BINGO_REDIS_ENDPOINT"
  "--sample.topology.redisKeyPrefix=$BINGO_REDIS_KEY_PREFIX"
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

start_server play-a "$PLAY_BIN" --sample.topology.playNode=a
wait_port play-a "$PLAY_A_CHANNEL_ENDPOINT"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER_ENDPOINT"
wait_port play-a-spot-pub "$PLAY_A_SPOT_ENDPOINT"
start_server play-b "$PLAY_BIN" --sample.topology.playNode=b
wait_port play-b "$PLAY_B_CHANNEL_ENDPOINT"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER_ENDPOINT"
wait_port play-b-spot-pub "$PLAY_B_SPOT_ENDPOINT"

start_server api-a "$API_BIN" --sample.topology.apiNode=a
wait_port api-a "$API_A_CHANNEL_ENDPOINT"
wait_port api-a-play-route "$API_A_PLAY_ROUTE_ENDPOINT"
start_server api-b "$API_BIN" --sample.topology.apiNode=b
wait_port api-b "$API_B_CHANNEL_ENDPOINT"
wait_port api-b-play-route "$API_B_PLAY_ROUTE_ENDPOINT"

start_server session-a "$SESSION_BIN" \
  --sample.topology.sessionNode=a \
  "--sample.topology.sessionSpotEndpoint=$SESSION_A_SPOT_ENDPOINT" \
  "--sample.topology.sessionRouterEndpoint=$SESSION_A_ROUTER_ENDPOINT" \
  "--sample.topology.streamEndpoint=$SESSION_A_STREAM_ENDPOINT"
wait_port session-a-router "$SESSION_A_ROUTER_ENDPOINT"
wait_port session-a-stream "$SESSION_A_STREAM_ENDPOINT"
wait_port session-a-play-route "$SESSION_A_PLAY_ROUTE_ENDPOINT"

start_server session-b "$SESSION_BIN" \
  --sample.topology.sessionNode=b \
  "--sample.topology.sessionSpotEndpoint=$SESSION_B_SPOT_ENDPOINT" \
  "--sample.topology.sessionRouterEndpoint=$SESSION_B_ROUTER_ENDPOINT" \
  "--sample.topology.streamEndpoint=$SESSION_B_STREAM_ENDPOINT"
wait_port session-b-router "$SESSION_B_ROUTER_ENDPOINT"
wait_port session-b-stream "$SESSION_B_STREAM_ENDPOINT"
wait_port session-b-play-route "$SESSION_B_PLAY_ROUTE_ENDPOINT"

sleep "${BINGO_STARTUP_SETTLE_SECONDS:-4}"

"$CLIENT_BIN" \
  --session-a-stream-endpoint "$SESSION_A_STREAM_ENDPOINT" \
  --session-b-stream-endpoint "$SESSION_B_STREAM_ENDPOINT" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/session-a.log" >&2
  cat "$LOG_DIR/session-b.log" >&2
  cat "$LOG_DIR/play-a.log" >&2
  cat "$LOG_DIR/play-b.log" >&2
  cat "$LOG_DIR/api-a.log" >&2
  cat "$LOG_DIR/api-b.log" >&2
  cat "$LOG_DIR/registry.log" >&2
  exit 1
}

grep -q "bingo=completed" "$LOG_DIR/client.log"
grep -q "stream-inbound sample=Bingo" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "$LOG_DIR/client.log"
grep -Rq "message flow" "$BINGO_LOG_DIR"

cleanup
trap - EXIT

echo "bingo full client/server self-check completed"
