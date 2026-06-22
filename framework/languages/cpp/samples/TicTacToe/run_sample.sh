#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
export TICTACTOE_LOG_DIR="${TICTACTOE_LOG_DIR:-$SCRIPT_DIR/logs}"
mkdir -p "$TICTACTOE_LOG_DIR"
rm -f "$TICTACTOE_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"

if [[ ! -x "$BIN_DIR/sample_cpp_framework_tictactoe_play" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_tictactoe_play" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PLAY_BIN="$BIN_DIR/sample_cpp_framework_tictactoe_play"
API_BIN="$BIN_DIR/sample_cpp_framework_tictactoe_api"
CLIENT_BIN="$BIN_DIR/sample_cpp_framework_tictactoe_client"
CTEST_BIN="${CTEST_BIN:-ctest}"

for binary in "$PLAY_BIN" "$API_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

"$CTEST_BIN" --test-dir "$BUILD_DIR" \
  -R 'test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay|sample_smoke_sample_cpp_framework_tictactoe_(play|api)' \
  --output-on-failure

if [[ -n "${TICTACTOE_CPP_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 15); do
    PORTS+=("$((TICTACTOE_CPP_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 15:
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

API_A_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
API_B_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"
API_A_HTTP_ENDPOINT="http://127.0.0.1:${PORTS[2]}"
API_B_HTTP_ENDPOINT="http://127.0.0.1:${PORTS[3]}"
PLAY_A_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
PLAY_B_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
PLAY_A_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
PLAY_B_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"
PLAY_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[8]}"
PLAY_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
PLAY_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[10]}"
PLAY_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[11]}"
PLAY_A_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[12]}"
PLAY_B_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[13]}"
REDIS_PORT="${PORTS[14]}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 120); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_grep() {
  local pattern="$1"
  local file="$2"
  for _ in $(seq 1 50); do
    if grep -q "$pattern" "$file"; then
      return 0
    fi
    sleep 0.1
  done
  grep -q "$pattern" "$file"
}

RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
PIDS=()
REDIS_CONTAINER=""
cleanup_done=false
REDIS_KEY_PREFIX="${TICTACTOE_CPP_REDIS_KEY_PREFIX:-zlink:tictactoe-cpp:${RANDOM}:$$:room:}"

cleanup() {
  if [[ "$cleanup_done" == true ]]; then
    return
  fi
  cleanup_done=true
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    kill "${PIDS[$i]}" 2>/dev/null || true
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ "${TICTACTOE_CPP_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    rm -rf "$RUN_DIR"
  fi
}
trap cleanup EXIT

# The sample owns its Redis: always provision a dedicated, throwaway container
# so room-route state stays isolated per run and never touches a developer's
# local Redis. (TICTACTOE_CPP_REDIS_ENDPOINT is intentionally derived here, not read.)
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the TicTacToe sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
REDIS_CONTAINER="zlink-tictactoe-cpp-redis-${RANDOM}-$$"
docker run -d --rm --name "$REDIS_CONTAINER" -p "127.0.0.1::6379" redis:7-alpine >/dev/null
TICTACTOE_CPP_REDIS_ENDPOINT="$(docker port "$REDIS_CONTAINER" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
wait_port redis "$TICTACTOE_CPP_REDIS_ENDPOINT"

topology_args=(
  "--sample.topology.apiEndpoint=$API_A_ENDPOINT"
  "--sample.topology.apiAEndpoint=$API_A_ENDPOINT"
  "--sample.topology.apiBEndpoint=$API_B_ENDPOINT"
  "--sample.topology.apiHttpEndpoint=$API_A_HTTP_ENDPOINT"
  "--sample.topology.apiAHttpEndpoint=$API_A_HTTP_ENDPOINT"
  "--sample.topology.apiBHttpEndpoint=$API_B_HTTP_ENDPOINT"
  "--sample.topology.playEndpoint=$PLAY_A_ENDPOINT"
  "--sample.topology.playAEndpoint=$PLAY_A_ENDPOINT"
  "--sample.topology.playBEndpoint=$PLAY_B_ENDPOINT"
  "--sample.topology.playARouteEndpoint=$PLAY_A_ROUTE_ENDPOINT"
  "--sample.topology.playBRouteEndpoint=$PLAY_B_ROUTE_ENDPOINT"
  "--sample.topology.playASpotEndpoint=$PLAY_A_SPOT_ENDPOINT"
  "--sample.topology.playBSpotEndpoint=$PLAY_B_SPOT_ENDPOINT"
  "--sample.topology.playASpotRouterEndpoint=$PLAY_A_SPOT_ROUTER_ENDPOINT"
  "--sample.topology.playBSpotRouterEndpoint=$PLAY_B_SPOT_ROUTER_ENDPOINT"
  "--sample.topology.playAStreamEndpoint=$PLAY_A_STREAM_ENDPOINT"
  "--sample.topology.playBStreamEndpoint=$PLAY_B_STREAM_ENDPOINT"
  "--sample.topology.redisEndpoint=$TICTACTOE_CPP_REDIS_ENDPOINT"
  "--sample.topology.redisKeyPrefix=$REDIS_KEY_PREFIX"
)

start_server() {
  local name="$1"
  local binary="$2"
  shift 2
  "$binary" --sample.host.keepRunning true "${topology_args[@]}" "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_server play-a "$PLAY_BIN" --sample.topology.playNode=a
wait_port play-a-channel "$PLAY_A_ENDPOINT"
wait_port play-a-stream "$PLAY_A_STREAM_ENDPOINT"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER_ENDPOINT"
wait_port play-a-spot-pub "$PLAY_A_SPOT_ENDPOINT"

start_server play-b "$PLAY_BIN" --sample.topology.playNode=b
wait_port play-b-channel "$PLAY_B_ENDPOINT"
wait_port play-b-stream "$PLAY_B_STREAM_ENDPOINT"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER_ENDPOINT"
wait_port play-b-spot-pub "$PLAY_B_SPOT_ENDPOINT"

start_server api-a "$API_BIN" --sample.topology.apiNode=a
wait_port api-a-channel "$API_A_ENDPOINT"
wait_port api-a-http "$API_A_HTTP_ENDPOINT"

start_server api-b "$API_BIN" --sample.topology.apiNode=b
wait_port api-b-channel "$API_B_ENDPOINT"
wait_port api-b-http "$API_B_HTTP_ENDPOINT"

sleep "${TICTACTOE_CPP_STARTUP_SETTLE_SECONDS:-1}"

"$CLIENT_BIN" --api-http-endpoint "$API_A_HTTP_ENDPOINT" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/play-a.log" >&2
  cat "$LOG_DIR/play-b.log" >&2
  cat "$LOG_DIR/api-a.log" >&2
  cat "$LOG_DIR/api-b.log" >&2
  exit 1
}

wait_grep "observer-connected endpoint=$PLAY_B_STREAM_ENDPOINT" "$LOG_DIR/client.log"
wait_grep "observer-subscription=verified subscribed=true" "$LOG_DIR/client.log"
wait_grep "observer-win-milestone=verified actor=player-x wins=100 receivingSpotNodeRid=play-node-2" "$LOG_DIR/client.log"
wait_grep "tictactoe completed" "$LOG_DIR/client.log"
wait_grep "actor: LeaveGameReq completed. actor=player-x" "$LOG_DIR/play-a.log"
wait_grep "actor: LeaveGameReq completed. actor=player-o" "$LOG_DIR/play-a.log"
grep -Rq "message flow" "$TICTACTOE_LOG_DIR"

cleanup
trap - EXIT

echo "PASS TicTacToe.Cpp"
echo "tictactoe full client/server self-check completed"
