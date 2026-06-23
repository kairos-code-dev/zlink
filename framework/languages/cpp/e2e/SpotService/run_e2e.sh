#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER ROUTE_A ROUTE_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_CLIENT PUB_A PUB_B PUB_CLIENT API_CLIENT HTTP_A HTTP_B <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(14):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:12]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[12:]))
for s in sockets:
    s.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_spot_service_server \
  zlink_cpp_e2e_spot_service_client >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_spot_service_server"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_spot_service_client"
PIDS=()

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  sleep 0.2
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill -9 "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
  exit "$code"
}
trap cleanup EXIT

port_of() {
  echo "${1##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 120); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

start_registry() {
  ZLINK_CPP_E2E_ROLE=registry \
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  PIDS+=("$!")
  wait_port registry-router "$REGISTRY_ROUTER"
}

start_play() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local pubsub="$4"
  local http="$5"
  ZLINK_CPP_E2E_ROLE=play \
  ZLINK_CPP_E2E_NODE_RID="$rid" \
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$pubsub" \
  ZLINK_CPP_E2E_API_PEER_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  PIDS+=("$!")
  wait_port "$rid-route" "$route"
  wait_port "$rid-spot" "$spot"
  wait_port "$rid-http" "$http"
}

fetch_evidence() {
  local name="$1"
  local http="$2"
  python3 - "$http/evidence" >"$LOG_DIR/$name-evidence.json" <<'PY'
import sys
import urllib.request

url = sys.argv[1]
with urllib.request.urlopen(url, timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
}

start_registry
start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
sleep 3

ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
fetch_evidence play-a "$HTTP_A"
fetch_evidence play-b "$HTTP_B"

python3 - "$LOG_DIR/play-a-evidence.json" "$LOG_DIR/play-b-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))

def has(snapshot, marker, actor=None):
    for entry in snapshot["entries"]:
        if entry["marker"] != marker:
            continue
        if actor is not None and entry["actor_id"] != actor:
            continue
        return True
    return False

assert has(play_a, "ActorEnsured", "alice")
assert has(play_a, "EntryJoin", "alice")
assert has(play_a, "StateMutated", "alice")
assert has(play_a, "ActorLeaveRequested", "alice")
assert has(play_a, "SpotOutbound", "alice-2")
assert has(play_a, "MeshEventReceived")
assert has(play_b, "ActorEnsured", "bob")
assert has(play_b, "EntryJoin", "bob")
print("spot-service evidence result=passed")
PY

echo "spot-service e2e result=passed"
