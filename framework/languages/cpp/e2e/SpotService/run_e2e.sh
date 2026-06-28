#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"
SCENARIO="${1:-all}"

read -r REGISTRY_PUB REGISTRY_ROUTER ROUTE_A ROUTE_B ROUTE_SESSION_A ROUTE_SESSION_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_SESSION_A SPOT_SESSION_B SPOT_CLIENT PUB_A PUB_B PUB_SESSION_A PUB_SESSION_B PUB_CLIENT PUBLISHER_CLIENT API_CLIENT STREAM_A STREAM_B HTTP_A HTTP_B HTTP_SESSION_A HTTP_SESSION_B HTTP_GATEWAY <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(26):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:21]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[21:]))
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
  zlink_cpp_e2e_spot_service_registry \
  zlink_cpp_e2e_spot_service_play \
  zlink_cpp_e2e_spot_service_session \
  zlink_cpp_e2e_spot_service_gateway \
  zlink_cpp_e2e_spot_service_client >/dev/null

REGISTRY_SERVER="$BUILD_DIR/zlink_cpp_e2e_spot_service_registry"
PLAY_SERVER="$BUILD_DIR/zlink_cpp_e2e_spot_service_play"
SESSION_SERVER="$BUILD_DIR/zlink_cpp_e2e_spot_service_session"
GATEWAY_SERVER="$BUILD_DIR/zlink_cpp_e2e_spot_service_gateway"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_spot_service_client"
PIDS=()
PLAY_A_PID=""
PLAY_B_PID=""
SESSION_A_PID=""
GATEWAY_PID=""

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

wait_port_closed() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 120); do
    if ! (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name to close at $endpoint" >&2
  return 1
}

wait_file() {
  local name="$1"
  local path="$2"
  for _ in $(seq 1 900); do
    if [[ -f "$path" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for $name file: $path" >&2
  return 1
}

start_registry() {
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$REGISTRY_SERVER" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  PIDS+=("$!")
  wait_port registry-router "$REGISTRY_ROUTER"
}

start_play() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local pubsub="$4"
  local http="$5"
  local api_server="${6:-}"
  ZLINK_CPP_E2E_NODE_RID="$rid" \
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$pubsub" \
  ZLINK_CPP_E2E_API_PEER_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$api_server" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PLAY_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  local pid="$!"
  PIDS+=("$pid")
  if [[ "$rid" == "play-a" ]]; then
    PLAY_A_PID="$pid"
  fi
  if [[ "$rid" == "play-b" ]]; then
    PLAY_B_PID="$pid"
  fi
  wait_port "$rid-route" "$route"
  wait_port "$rid-spot" "$spot"
  wait_port "$rid-http" "$http"
}

start_session() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local pubsub="$4"
  local stream="$5"
  local http="$6"
  ZLINK_CPP_E2E_NODE_RID="$rid" \
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$pubsub" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$stream" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SESSION_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  local pid="$!"
  PIDS+=("$pid")
  if [[ "$rid" == "session-a" ]]; then
    SESSION_A_PID="$pid"
  fi
  wait_port "$rid-route" "$route"
  wait_port "$rid-spot" "$spot"
  wait_port "$rid-stream" "$stream"
  wait_port "$rid-http" "$http"
}

start_gateway() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local pubsub="$4"
  local http="$5"
  ZLINK_CPP_E2E_NODE_RID="$rid" \
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$pubsub" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$GATEWAY_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  local pid="$!"
  PIDS+=("$pid")
  GATEWAY_PID="$pid"
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

run_base_client() {
  local mode="$1"
  local output="$2"
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE="$mode" \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-$mode" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/$output.stdout.log" 2>"$LOG_DIR/$output.stderr.log"
  cat "$LOG_DIR/$output.stdout.log"
}

if [[ "$SCENARIO" == "SM-A1" || "$SCENARIO" == "sm-a1" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 2
  run_base_client sm-a1 client-sm-a1
  fetch_evidence play-a-sm-a1 "$HTTP_A"
  python3 - "$LOG_DIR/play-a-sm-a1-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
assert any(entry["marker"] == "EntryJoin"
           and entry["actor_id"] == "alice"
           and entry["value"] == "a-room"
           for entry in play_a["entries"])
assert any(entry["marker"] == "ActorJoined"
           and entry["actor_id"] == "alice"
           and entry["spot_rid"] == "user:play-a:a-room"
           for entry in play_a["entries"])
print("scenario SM-A1 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-A2" || "$SCENARIO" == "sm-a2" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-a2 client-sm-a2
  fetch_evidence play-a-sm-a2 "$HTTP_A"
  python3 - "$LOG_DIR/play-a-sm-a2-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
values = [entry["value"]
          for entry in play_a["entries"]
          if entry["marker"] == "StateMutated"
          and entry["actor_id"] == "alice"
          and entry["spot_rid"] == "user:play-a:a-room"]
assert len(values) == 4, values
assert values[:2] == ["3", "7"], values
assert values[-1] == "18", values
assert values[2] in ("12", "13"), values
print("scenario SM-A2 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-A3" || "$SCENARIO" == "sm-a3" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-a3 client-sm-a3
  fetch_evidence play-a-sm-a3 "$HTTP_A"
  fetch_evidence play-b-sm-a3 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-a3-evidence.json" "$LOG_DIR/play-b-sm-a3-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-a:sm-a3-route"
assert any(entry["marker"] == "SpotToSpotRequest"
           and entry["actor_id"] == "sm-a3-client"
           and entry["spot_rid"] == spot
           and entry["value"] == "route-direct"
           for entry in play_a["entries"])
assert not any(entry["spot_rid"] == spot for entry in play_b["entries"])
print("scenario SM-A3 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-A4" || "$SCENARIO" == "sm-a4" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-a4 client-sm-a4
  fetch_evidence play-a-sm-a4 "$HTTP_A"
  fetch_evidence play-b-sm-a4 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-a4-evidence.json" "$LOG_DIR/play-b-sm-a4-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-a:sm-a4-owner"
values = [entry["value"]
          for entry in play_a["entries"]
          if entry["marker"] == "StateRouted"
          and entry["spot_rid"] == spot]
assert len(values) == 2, values
assert all(value == "0" for value in values), values
assert not any(entry["spot_rid"] == spot for entry in play_b["entries"])
print("scenario SM-A4 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-A6" || "$SCENARIO" == "sm-a6" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 2
  run_base_client sm-a6 client-sm-a6
  fetch_evidence play-a-sm-a6 "$HTTP_A"
  fetch_evidence play-b-sm-a6 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-a6-evidence.json" "$LOG_DIR/play-b-sm-a6-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
life = "user:play-a:sm-a6-life"
busy = "user:play-a:sm-a6-busy"
initialized = [entry for entry in play_a["entries"]
               if entry["marker"] == "SpotInitialized"
               and entry["spot_rid"] == life]
closing = [entry for entry in play_a["entries"]
           if entry["marker"] == "SpotClosing"
           and entry["spot_rid"] == life]
closed = [entry for entry in play_a["entries"]
          if entry["marker"] == "SpotLifecycleClosed"
          and entry["spot_rid"] == life
          and entry["value"] == "closed"]
assert len(initialized) == 1, initialized
assert len(closing) == 1, closing
assert len(closed) == 1, closed
assert any(entry["marker"] == "ActorJoined"
           and entry["actor_id"] == "sm-a6-actor"
           and entry["spot_rid"] == busy
           for entry in play_a["entries"])
assert any(entry["marker"] == "SpotCloseRequested"
           and entry["spot_rid"] == busy
           and entry["value"] == "not-closed"
           for entry in play_a["entries"])
assert not any(entry["marker"] == "SpotClosing" and entry["spot_rid"] == busy
               for entry in play_a["entries"])
assert not any(entry["spot_rid"] in (life, busy) for entry in play_b["entries"])
print("scenario SM-A6 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-A7" || "$SCENARIO" == "sm-a7" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-a7 client-sm-a7
  fetch_evidence play-a-sm-a7 "$HTTP_A"
  fetch_evidence play-b-sm-a7 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-a7-evidence.json" "$LOG_DIR/play-b-sm-a7-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-a:sm-a7-mismatch"
mismatches = [entry for entry in play_a["entries"]
              if entry["marker"] == "SpotTypeMismatch"
              and entry["spot_rid"] == spot
              and entry["value"] == "user"]
state_values = [entry["value"] for entry in play_a["entries"]
                if entry["marker"] == "StateMutated"
                and entry["spot_rid"] == spot]
assert len(mismatches) == 1, mismatches
assert state_values == ["17", "17"], state_values
assert not any(entry["spot_rid"] == spot for entry in play_b["entries"])
print("scenario SM-A7 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-A8" || "$SCENARIO" == "sm-a8" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-a8 client-sm-a8
  fetch_evidence play-a-sm-a8 "$HTTP_A"
  fetch_evidence play-b-sm-a8 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-a8-evidence.json" "$LOG_DIR/play-b-sm-a8-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-a:sm-a8-worker"
actor = "sm-a8-actor"
entries = play_a["entries"]

def indices(marker, value=None, actor_id=None):
    result = []
    for index, entry in enumerate(entries):
        if entry["marker"] != marker or entry["spot_rid"] != spot:
            continue
        if actor_id is not None and entry["actor_id"] != actor_id:
            continue
        if value is not None and entry["value"] != value:
            continue
        result.append(index)
    return result

started = indices("WorkerStarted", "7", actor)
interleaved = indices("StateRouted", "12")
completed = indices("WorkerCompleted", "25", actor)
assert len(started) == 1, started
assert len(interleaved) == 1, interleaved
assert len(completed) == 1, completed
assert started[0] < interleaved[0] < completed[0], (started, interleaved, completed)
assert not any(entry["spot_rid"] == spot for entry in play_b["entries"])
print("scenario SM-A8 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B1" || "$SCENARIO" == "sm-b1" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 2
  run_base_client sm-b1 client-sm-b1
  fetch_evidence play-a-sm-b1 "$HTTP_A"
  fetch_evidence play-b-sm-b1 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-b1-evidence.json" "$LOG_DIR/play-b-sm-b1-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
actor = "sm-b1-local"
spot = "user:play-a:sm-b1-local"
markers = ["ActorCreated", "EntryActorJoined", "ActorJoined", "ActorJoinedCallback",
           "StateMutated"]
indices = []
for marker in markers:
    matches = [index for index, entry in enumerate(play_a["entries"])
               if entry["marker"] == marker
               and entry["actor_id"] == actor
               and (marker in ("ActorCreated", "EntryActorJoined")
                    or entry["spot_rid"] == spot)]
    assert len(matches) == 1, (marker, matches)
    indices.append(matches[0])
assert indices == sorted(indices), indices
assert any(entry["marker"] == "StateMutated"
           and entry["actor_id"] == actor
           and entry["spot_rid"] == spot
           and entry["value"] == "1"
           for entry in play_a["entries"])
assert not any(entry["actor_id"] == actor or entry["spot_rid"] == spot
               for entry in play_b["entries"])
print("scenario SM-B1 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B2" || "$SCENARIO" == "sm-b2" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-b2 client-sm-b2
  fetch_evidence play-a-sm-b2 "$HTTP_A"
  fetch_evidence play-b-sm-b2 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-b2-evidence.json" "$LOG_DIR/play-b-sm-b2-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
actor = "sm-b2-remote"
entry = "play-b:entry:1"
spot = "user:play-b:b-sm-b2-remote"
markers = ["ActorCreated", "EntryActorJoined", "ActorJoined", "ActorJoinedCallback",
           "StateMutated"]
indices = []
for marker in markers:
    matches = [index for index, item in enumerate(play_b["entries"])
               if item["marker"] == marker
               and item["actor_id"] == actor
               and (item["spot_rid"] == entry or item["spot_rid"] == spot)]
    assert len(matches) == 1, (marker, matches)
    indices.append(matches[0])
assert indices == sorted(indices), indices
assert any(item["marker"] == "EntryJoin"
           and item["actor_id"] == actor
           and item["spot_rid"] == entry
           and item["value"] == "b-sm-b2-remote"
           for item in play_b["entries"])
assert any(item["marker"] == "StateMutated"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "2"
           for item in play_b["entries"])
assert not any(item["actor_id"] == actor or item["spot_rid"] == spot
               for item in play_a["entries"])
print("scenario SM-B2 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B3" || "$SCENARIO" == "sm-b3" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-b3 client-sm-b3
  fetch_evidence play-a-sm-b3 "$HTTP_A"
  fetch_evidence play-b-sm-b3 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-b3-evidence.json" "$LOG_DIR/play-b-sm-b3-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
actor = "sm-b3-complex"
entry = "play-a:entry:1"
spot = "user:play-a:sm-b3-complex"
markers = ["ActorCreated", "EntryActorJoined", "ActorJoined", "ActorJoinedCallback",
           "ActorComplex"]
indices = []
for marker in markers:
    matches = [index for index, item in enumerate(play_a["entries"])
               if item["marker"] == marker
               and item["actor_id"] == actor
               and (item["spot_rid"] == entry or item["spot_rid"] == spot)]
    assert len(matches) == 1, (marker, matches)
    indices.append(matches[0])
assert indices == sorted(indices), indices
assert any(item["marker"] == "EntryJoin"
           and item["actor_id"] == actor
           and item["spot_rid"] == entry
           and item["value"] == "sm-b3-complex"
           for item in play_a["entries"])
assert any(item["marker"] == "ActorComplex"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "Ada Lovelace|42|analyst|west"
           for item in play_a["entries"])
assert not any(item["actor_id"] == actor or item["spot_rid"] == spot
               for item in play_b["entries"])
print("scenario SM-B3 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B4" || "$SCENARIO" == "sm-b4" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-b4 client-sm-b4
  fetch_evidence play-a-sm-b4 "$HTTP_A"
  fetch_evidence play-b-sm-b4 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-b4-evidence.json" "$LOG_DIR/play-b-sm-b4-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
actor = "sm-b4-remote"
spot = "user:play-b:b-sm-b4-remote"
assert any(item["marker"] == "RemoteActorRequestSent"
           and item["actor_id"] == actor
           and item["value"] == "play-b:14"
           for item in play_a["entries"])
assert any(item["marker"] == "RemoteActorRequestReply"
           and item["actor_id"] == actor
           and item["value"] == "play-b:14"
           for item in play_a["entries"])
assert any(item["marker"] == "ActorEnsured"
           and item["actor_id"] == actor
           for item in play_b["entries"])
assert any(item["marker"] == "RemoteUserSpotEnsured"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "b-sm-b4-remote"
           for item in play_b["entries"])
assert any(item["marker"] == "ActorJoined"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "b-sm-b4-remote"
           for item in play_b["entries"])
state = [index for index, item in enumerate(play_b["entries"])
         if item["marker"] == "StateMutated"
         and item["actor_id"] == actor
         and item["spot_rid"] == spot
         and item["value"] == "14"]
assert len(state) == 1, state
assert not any(item["marker"] in ("ActorEnsured", "RemoteUserSpotEnsured", "ActorJoined",
                                  "StateMutated")
               and (item["actor_id"] == actor or item["spot_rid"] == spot)
               for item in play_a["entries"])
print("scenario SM-B4 evidence passed")
PY
  grep -q "packet=__zlink.spot.actor.packet.*src=play-a" "$LOG_DIR/play-b-flow.log"
  grep -q "packet=__zlink.spot.actor.packet.*src=play-b" "$LOG_DIR/play-a-flow.log"
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B5" || "$SCENARIO" == "sm-b5" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  run_base_client sm-b5 client-sm-b5
  fetch_evidence play-a-sm-b5 "$HTTP_A"
  fetch_evidence play-b-sm-b5 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-b5-evidence.json" "$LOG_DIR/play-b-sm-b5-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
actor = "sm-b5-missing"
spot = "user:play-a:sm-b5-missing"
assert any(item["marker"] == "ActorJoined"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           for item in play_a["entries"])
assert not any((item["actor_id"] == actor or item["spot_rid"] == spot)
               for item in play_b["entries"])
print("scenario SM-B5 evidence passed")
PY
  grep -q "surface=spot_actor.*reason=handler_missing.*action=reply_error.*packet=MissingActorPacket" \
    "$LOG_DIR/play-a.stderr.log"
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B6" || "$SCENARIO" == "sm-b6" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-b6 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-b6" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-b6.stdout.log" 2>"$LOG_DIR/client-sm-b6.stderr.log"
  cat "$LOG_DIR/client-sm-b6.stdout.log"
  fetch_evidence play-a-sm-b6 "$HTTP_A"
  fetch_evidence play-b-sm-b6 "$HTTP_B"
  fetch_evidence session-a-sm-b6 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-b6-evidence.json" "$LOG_DIR/play-b-sm-b6-evidence.json" "$LOG_DIR/session-a-sm-b6-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
left = "sm-b6-left"
disconnected = "sm-b6-disconnect-d5-notified"
left_spot = "user:play-a:sm-b6-left"
disconnect_spot = "user:play-a:sm-b6-disconnect"

def count(snapshot, marker, actor):
    return sum(1 for item in snapshot["entries"]
               if item["marker"] == marker and item["actor_id"] == actor)

def has(snapshot, marker, actor, spot=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor
               and (spot is None or item["spot_rid"] == spot)
               for item in snapshot["entries"])

assert count(play_a, "ActorLeft", left) == 1
assert has(play_a, "ActorLeft", left, left_spot)
assert count(play_a, "ActorDisconnected", left) == 0
assert count(play_a, "ActorDisconnected", disconnected) == 1
assert has(play_a, "ActorDisconnected", disconnected, disconnect_spot)
assert count(play_a, "ActorLeft", disconnected) == 0
assert not any(item["actor_id"] in (left, disconnected) for item in play_b["entries"])
assert has(session_a, "StreamDisconnectNotified", disconnected)
assert has(session_a, "StreamUnbound", disconnected)
assert has(session_a, "StreamUnbound", left)
print("scenario SM-B6 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B7" || "$SCENARIO" == "sm-b7" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-b7 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-b7" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-b7.stdout.log" 2>"$LOG_DIR/client-sm-b7.stderr.log"
  cat "$LOG_DIR/client-sm-b7.stdout.log"
  fetch_evidence play-a-sm-b7 "$HTTP_A"
  fetch_evidence play-b-sm-b7 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-b7-evidence.json" "$LOG_DIR/play-b-sm-b7-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
actor = "sm-b7-order"
entry = "play-a:entry:1"
spot = "user:play-a:sm-b7-order"
entries = play_a["entries"]

def first_index(marker, spot_rid=None, value=None):
    matches = [index for index, item in enumerate(entries)
               if item["marker"] == marker
               and item["actor_id"] == actor
               and (spot_rid is None or item["spot_rid"] == spot_rid)
               and (value is None or item["value"] == value)]
    assert len(matches) == 1, (marker, spot_rid, value, matches)
    return matches[0]

created = first_index("ActorCreated", entry)
entry_joined = first_index("EntryActorJoined", entry)
joined = first_index("ActorJoined", spot, "sm-b7-order")
joined_callback = first_index("ActorJoinedCallback", spot)
first_ping = first_index("ActorPing", spot, "order-1:1")
second_ping = first_index("ActorPing", spot, "order-2:2")
assert [created, entry_joined, joined, joined_callback, first_ping, second_ping] == sorted(
    [created, entry_joined, joined, joined_callback, first_ping, second_ping])
assert not any(item["actor_id"] == actor or item["spot_rid"] == spot
               for item in play_b["entries"])
print("scenario SM-B7 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-B8" || "$SCENARIO" == "sm-b8" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-b8 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-b8" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-b8.stdout.log" 2>"$LOG_DIR/client-sm-b8.stderr.log"
  cat "$LOG_DIR/client-sm-b8.stdout.log"
  fetch_evidence play-a-sm-b8 "$HTTP_A"
  fetch_evidence play-b-sm-b8 "$HTTP_B"
  fetch_evidence session-a-sm-b8 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-b8-evidence.json" "$LOG_DIR/play-b-sm-b8-evidence.json" "$LOG_DIR/session-a-sm-b8-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "sm-b8-destroy"
entry = "play-a:entry:1"
spot = "user:play-a:sm-b8-destroy"
entries = play_a["entries"]

def indexes(marker, spot_rid=None, value=None):
    return [index for index, item in enumerate(entries)
            if item["marker"] == marker
            and item["actor_id"] == actor
            and (spot_rid is None or item["spot_rid"] == spot_rid)
            and (value is None or item["value"] == value)]

created = indexes("ActorCreated", entry)
joined = indexes("ActorJoined", spot, "sm-b8-destroy")
left = indexes("ActorLeft", spot)
destroyed = indexes("ActorDestroyed", entry, "explicit")
assert len(created) == 1, created
assert len(joined) == 1, joined
assert len(left) == 1, left
assert len(destroyed) == 1, destroyed
assert created[0] < joined[0] < left[0] < destroyed[0]
assert not indexes("ActorPing", spot, "after-destroy:1")
assert not any(item["actor_id"] == actor or item["spot_rid"] in (entry, spot)
               for item in play_b["entries"])
assert any(item["marker"] == "StreamUnbound" and item["actor_id"] == actor
           for item in session_a["entries"])
print("scenario SM-B8 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-C1" || "$SCENARIO" == "sm-c1" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-c1 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-c1" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-c1.stdout.log" 2>"$LOG_DIR/client-sm-c1.stderr.log"
  cat "$LOG_DIR/client-sm-c1.stdout.log"
  sleep 1
  fetch_evidence play-a-sm-c1 "$HTTP_A"
  fetch_evidence play-b-sm-c1 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-c1-evidence.json" "$LOG_DIR/play-b-sm-c1-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-a:sm-c1-channel"
entries = play_a["entries"]

def has(marker, value=None, actor_id=None):
    return any(item["marker"] == marker
               and item["spot_rid"] == spot
               and (value is None or item["value"] == value)
               and (actor_id is None or item["actor_id"] == actor_id)
               for item in entries)

assert has("SpotInitialized")
assert has("SpotToSpotRequest", "sm-c1-request", "sm-c1-client")
assert has("SpotToSpotCommand", "sm-c1-send", "sm-c1-client")
assert has("MeshEventReceived", "evt-sm-c1:sm-c1-publish")
assert has("SpotToSpotRequest", "sm-c1-after-timeout", "sm-c1-client")
assert not any(item["spot_rid"] == spot for item in play_b["entries"])
print("scenario SM-C1 evidence passed")
PY
  grep -q "surface=spot_route.*reason=handler_missing.*action=reply_error.*packet=MissingSpotReq" \
    "$LOG_DIR/play-a.stderr.log"
  grep -q "surface=spot_route.*reason=handler_missing.*action=drop.*packet=MissingSpotSend" \
    "$LOG_DIR/play-a.stderr.log"
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-C2" || "$SCENARIO" == "sm-c2" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A" "$API_CLIENT"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-c2 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-c2" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-c2.stdout.log" 2>"$LOG_DIR/client-sm-c2.stderr.log"
  cat "$LOG_DIR/client-sm-c2.stdout.log"
  sleep 1
  fetch_evidence play-a-sm-c2 "$HTTP_A"
  fetch_evidence play-b-sm-c2 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-c2-evidence.json" "$LOG_DIR/play-b-sm-c2-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-b:sm-c2-outbound"

def has(snapshot, marker, value=None, spot_rid=None):
    return any(item["marker"] == marker
               and (value is None or item["value"] == value)
               and (spot_rid is None or item["spot_rid"] == spot_rid)
               for item in snapshot["entries"])

assert has(play_b, "SpotInitialized", spot_rid=spot)
assert has(play_b, "SpotOutbound", "echo-sm-c2|notify-sm-c2|timeout=true", spot)
assert has(play_b, "MeshEventReceived", "evt-sm-c2:sm-c2-publish", spot)
assert has(play_b, "SpotOutboundNegative", "requestFailed=true", spot)
assert has(play_a, "ChannelEcho", "sm-c2")
assert has(play_a, "ChannelCommand", "notify-sm-c2")
assert has(play_a, "ChannelSlow", "sm-c2")
assert not any(item["spot_rid"] == spot for item in play_a["entries"])
print("scenario SM-C2 evidence passed")
PY
  grep -q "surface=channel.*reason=handler_missing.*action=reply_error.*packet=MissingChannelReq" \
    "$LOG_DIR/play-a-flow.log"
  grep -q "surface=channel.*reason=handler_missing.*action=drop.*packet=MissingChannelSend" \
    "$LOG_DIR/play-a-flow.log"
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-C3" || "$SCENARIO" == "sm-c3" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-c3 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-c3" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-c3.stdout.log" 2>"$LOG_DIR/client-sm-c3.stderr.log"
  cat "$LOG_DIR/client-sm-c3.stdout.log"
  sleep 1
  fetch_evidence play-a-sm-c3 "$HTTP_A"
  fetch_evidence play-b-sm-c3 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-c3-evidence.json" "$LOG_DIR/play-b-sm-c3-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
source = "user:play-b:sm-c3-source"
target = "user:play-a:sm-c3-target"

def has(snapshot, marker, value=None, spot_rid=None, actor_id=None):
    return any(item["marker"] == marker
               and (value is None or item["value"] == value)
               and (spot_rid is None or item["spot_rid"] == spot_rid)
               and (actor_id is None or item["actor_id"] == actor_id)
               for item in snapshot["entries"])

assert has(play_b, "SpotInitialized", spot_rid=source)
assert has(play_a, "SpotInitialized", spot_rid=target)
assert has(play_b, "SpotToSpotOutbound",
           f"target={target}|value=sm-c3-direct:reply", source)
assert has(play_a, "SpotToSpotRequest", "sm-c3-direct", target, source)
assert has(play_a, "SpotToSpotCommand", "sm-c3-send-direct", target, source)
assert has(play_a, "MeshEventReceived", "evt-sm-c3:sm-c3-publish-direct", target)
assert has(play_b, "SpotToSpotTimeout", f"target={target}|failed=true", source)
assert has(play_b, "SpotToSpotNegative", f"target={target}|requestFailed=true", source)
print("scenario SM-C3 evidence passed")
PY
  grep -q "surface=spot_route.*reason=handler_missing.*action=reply_error.*packet=MissingSpotReq" \
    "$LOG_DIR/play-a.stderr.log"
  grep -q "surface=spot_route.*reason=handler_missing.*action=drop.*packet=MissingSpotCommand" \
    "$LOG_DIR/play-a.stderr.log"
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-C4" || "$SCENARIO" == "sm-c4" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_gateway gateway "$ROUTE_CLIENT" "$SPOT_CLIENT" "$PUB_CLIENT" "$HTTP_GATEWAY"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-c4 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_GATEWAY_HTTP_ENDPOINT="$HTTP_GATEWAY" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-c4" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-c4.stdout.log" 2>"$LOG_DIR/client-sm-c4.stderr.log"
  cat "$LOG_DIR/client-sm-c4.stdout.log"
  sleep 1
  fetch_evidence play-a-sm-c4 "$HTTP_A"
  fetch_evidence gateway-sm-c4 "$HTTP_GATEWAY"
  python3 - "$LOG_DIR/play-a-sm-c4-evidence.json" "$LOG_DIR/gateway-sm-c4-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
gateway = json.load(open(sys.argv[2], encoding="utf-8"))
subscribed = "user:play-a:sm-c4-subscribed"
unsubscribed = "user:play-a:sm-c4-unsubscribed"

def count_event(spot_rid):
    return sum(1 for item in play_a["entries"]
               if item["marker"] == "MeshEventReceived"
               and item["spot_rid"] == spot_rid
               and item["value"] == "evt-sm-c4:sm-c4-publish")

assert any(item["marker"] == "SpotInitialized" and item["spot_rid"] == subscribed
           for item in play_a["entries"])
assert count_event(subscribed) >= 1
assert count_event(unsubscribed) == 0
assert any(item["marker"] == "SpotPublish"
           and item["spot_rid"] == subscribed
           and item["value"] == "publisher=gateway|marker=sm-c4-publish"
           for item in gateway["entries"])
assert not any(item["marker"] == "SpotInitialized" for item in gateway["entries"])
print("scenario SM-C4 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D1" || "$SCENARIO" == "sm-d1" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d1 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d1" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d1.stdout.log" 2>"$LOG_DIR/client-sm-d1.stderr.log"
  cat "$LOG_DIR/client-sm-d1.stdout.log"
  fetch_evidence play-a-sm-d1 "$HTTP_A"
  fetch_evidence play-b-sm-d1 "$HTTP_B"
  fetch_evidence session-a-sm-d1 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d1-evidence.json" "$LOG_DIR/play-b-sm-d1-evidence.json" "$LOG_DIR/session-a-sm-d1-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d1"
spot = "user:play-a:sm-d1-local"

assert any(item["marker"] == "StreamBound"
           and item["actor_id"] == actor
           and item["value"].startswith("play-a:")
           for item in session_a["entries"])
assert any(item["marker"] == "ActorPing"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "local-relay:1"
           for item in play_a["entries"])
assert any(item["marker"] == "ActorPushedSession"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "push-local"
           for item in play_a["entries"])
assert not any(item["actor_id"] == actor or item["spot_rid"] == spot
               for item in play_b["entries"])
print("scenario SM-D1 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D2" || "$SCENARIO" == "sm-d2" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d2 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d2" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d2.stdout.log" 2>"$LOG_DIR/client-sm-d2.stderr.log"
  cat "$LOG_DIR/client-sm-d2.stdout.log"
  fetch_evidence play-a-sm-d2 "$HTTP_A"
  fetch_evidence play-b-sm-d2 "$HTTP_B"
  fetch_evidence session-a-sm-d2 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d2-evidence.json" "$LOG_DIR/play-b-sm-d2-evidence.json" "$LOG_DIR/session-a-sm-d2-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d2"
spot = "user:play-b:b-sm-d2-remote"

assert any(item["marker"] == "StreamBound"
           and item["actor_id"] == actor
           and item["value"].startswith("play-b:")
           for item in session_a["entries"])
assert any(item["marker"] == "ActorPing"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "remote-relay:1"
           for item in play_b["entries"])
assert any(item["marker"] == "ActorPushedSession"
           and item["actor_id"] == actor
           and item["spot_rid"] == spot
           and item["value"] == "push-remote"
           for item in play_b["entries"])
assert not any(item["actor_id"] == actor or item["spot_rid"] == spot
               for item in play_a["entries"])
print("scenario SM-D2 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D3" || "$SCENARIO" == "sm-d3" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d3 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d3" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d3.stdout.log" 2>"$LOG_DIR/client-sm-d3.stderr.log"
  cat "$LOG_DIR/client-sm-d3.stdout.log"
  fetch_evidence play-a-sm-d3 "$HTTP_A"
  fetch_evidence play-b-sm-d3 "$HTTP_B"
  fetch_evidence session-a-sm-d3 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d3-evidence.json" "$LOG_DIR/play-b-sm-d3-evidence.json" "$LOG_DIR/session-a-sm-d3-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
entry_actor = "actor-sm-d3-entry"
user_actor = "actor-sm-d3-user"
entry_spot = "play-a:entry:1"
user_spot = "user:play-a:sm-d3-user"

def has(snapshot, marker, actor, spot=None, value=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor
               and (spot is None or item["spot_rid"] == spot)
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "ActorEnsured", entry_actor)
assert has(play_a, "EntryActorPing", entry_actor, entry_spot, "entry-relay:1")
assert has(play_a, "EntryActorPushedSession", entry_actor, entry_spot, "entry-push")
assert has(play_a, "ActorJoined", user_actor, user_spot, "sm-d3-user")
assert has(play_a, "ActorPing", user_actor, user_spot, "user-relay:1")
assert has(play_a, "ActorPushedSession", user_actor, user_spot, "user-push")
assert has(session_a, "StreamBound", entry_actor)
assert has(session_a, "StreamBound", user_actor)
assert not any(item["actor_id"] in (entry_actor, user_actor)
               or item["spot_rid"] in (entry_spot, user_spot)
               for item in play_b["entries"])
print("scenario SM-D3 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D4" || "$SCENARIO" == "sm-d4" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d4 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d4" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d4.stdout.log" 2>"$LOG_DIR/client-sm-d4.stderr.log"
  cat "$LOG_DIR/client-sm-d4.stdout.log"
  fetch_evidence play-a-sm-d4 "$HTTP_A"
  fetch_evidence play-b-sm-d4 "$HTTP_B"
  fetch_evidence session-a-sm-d4 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d4-evidence.json" "$LOG_DIR/play-b-sm-d4-evidence.json" "$LOG_DIR/session-a-sm-d4-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
first = "actor-sm-d4-x"
second = "actor-sm-d4-y"
entry = "play-a:entry:1"

def has(snapshot, marker, actor, value=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "EntryActorPing", first, "to-x:1")
assert has(play_a, "EntryActorPing", second, "to-y:1")
assert has(play_a, "EntryActorPushedSession", first, "push-x")
assert has(play_a, "EntryActorPushedSession", second, "push-y")
assert has(session_a, "StreamBound", first)
assert has(session_a, "StreamBound", second)
assert not any(item["actor_id"] in (first, second) or item["spot_rid"] == entry
               for item in play_b["entries"])
print("scenario SM-D4 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D5" || "$SCENARIO" == "sm-d5" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d5 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d5" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d5.stdout.log" 2>"$LOG_DIR/client-sm-d5.stderr.log"
  cat "$LOG_DIR/client-sm-d5.stdout.log"
  sleep 1
  fetch_evidence play-a-sm-d5 "$HTTP_A"
  fetch_evidence play-b-sm-d5 "$HTTP_B"
  fetch_evidence session-a-sm-d5 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d5-evidence.json" "$LOG_DIR/play-b-sm-d5-evidence.json" "$LOG_DIR/session-a-sm-d5-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
notified = "stream-disconnect-d5-notified"
muted = "stream-disconnect-d5-muted"
single = "stream-disconnect-d5-notified-single"
remote = "stream-disconnect-d5-notified-remote"
notified_spot = "user:play-a:a-stream-disconnect-notified"
muted_spot = "user:play-a:a-stream-disconnect-muted"
single_spot = "user:play-a:a-stream-disconnect-single"
remote_spot = "user:play-b:b-stream-disconnect-remote"

def count(snapshot, marker, actor):
    return sum(1 for item in snapshot["entries"]
               if item["marker"] == marker and item["actor_id"] == actor)

assert count(play_a, "ActorDisconnected", single) == 1
assert count(play_a, "ActorDisconnected", notified) == 1
assert count(play_a, "ActorDisconnected", muted) == 0
assert count(play_b, "ActorDisconnected", remote) == 1
assert count(play_a, "ActorDisconnected", remote) == 0
assert count(play_b, "ActorDisconnected", notified) == 0
assert count(play_b, "ActorDisconnected", muted) == 0
assert count(play_a, "ActorLeft", single) == 0
assert count(play_a, "ActorLeft", notified) == 0
assert count(play_a, "ActorLeft", muted) == 0
assert count(play_b, "ActorLeft", remote) == 0
assert count(session_a, "StreamDisconnectNotified", single) == 1
assert count(session_a, "StreamDisconnectNotified", notified) == 1
assert count(session_a, "StreamDisconnectNotified", remote) == 1
assert count(session_a, "StreamDisconnectNotified", muted) == 0
assert count(session_a, "StreamUnbound", single) == 1
assert count(session_a, "StreamUnbound", notified) == 1
assert count(session_a, "StreamUnbound", muted) == 1
assert count(session_a, "StreamUnbound", remote) == 1
assert not any(item["actor_id"] in (single, notified, muted)
               or item["spot_rid"] in (single_spot, notified_spot, muted_spot)
               for item in play_b["entries"])
assert not any(item["actor_id"] == remote or item["spot_rid"] == remote_spot
               for item in play_a["entries"])
print("scenario SM-D5 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D6" || "$SCENARIO" == "sm-d6" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  start_session session-b "$ROUTE_SESSION_B" "$SPOT_SESSION_B" "$PUB_SESSION_B" "$STREAM_B" "$HTTP_SESSION_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT="$STREAM_B" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d6 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d6" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d6.stdout.log" 2>"$LOG_DIR/client-sm-d6.stderr.log"
  cat "$LOG_DIR/client-sm-d6.stdout.log"
  fetch_evidence play-a-sm-d6 "$HTTP_A"
  fetch_evidence play-b-sm-d6 "$HTTP_B"
  fetch_evidence session-a-sm-d6 "$HTTP_SESSION_A"
  fetch_evidence session-b-sm-d6 "$HTTP_SESSION_B"
  python3 - "$LOG_DIR/play-a-sm-d6-evidence.json" "$LOG_DIR/play-b-sm-d6-evidence.json" "$LOG_DIR/session-a-sm-d6-evidence.json" "$LOG_DIR/session-b-sm-d6-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
session_b = json.load(open(sys.argv[4], encoding="utf-8"))
actor = "actor-sm-d6"
shadow = "actor-sm-d6-shadow"

def has(snapshot, marker, actor_id, value=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor_id
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "EntryActorPushedSession", actor, "push-bound-only")
assert not has(play_b, "EntryActorPushedSession", actor, "push-bound-only")
assert not has(play_b, "EntryActorPushedSession", shadow, "push-bound-only")
assert has(session_a, "StreamBound", actor)
assert has(session_b, "StreamBound", shadow)
print("scenario SM-D6 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D7" || "$SCENARIO" == "sm-d7" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d7 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d7" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d7.stdout.log" 2>"$LOG_DIR/client-sm-d7.stderr.log"
  cat "$LOG_DIR/client-sm-d7.stdout.log"
  fetch_evidence play-a-sm-d7 "$HTTP_A"
  fetch_evidence play-b-sm-d7 "$HTTP_B"
  fetch_evidence session-a-sm-d7 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d7-evidence.json" "$LOG_DIR/play-b-sm-d7-evidence.json" "$LOG_DIR/session-a-sm-d7-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d7"
invalid = "actor-sm-d7-invalid"

def has(snapshot, marker, actor_id, value=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor_id
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "ActorEnsured", actor, "SM-D7 Auth")
assert has(play_a, "EntryActorPing", actor, "auth-ok:1")
assert has(session_a, "StreamBound", actor)
assert has(session_a, "StreamAuthFailed", invalid, "play-a")
assert not any(item["actor_id"] in (actor, invalid) for item in play_b["entries"])
print("scenario SM-D7 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D8" || "$SCENARIO" == "sm-d8" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d8 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d8" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d8.stdout.log" 2>"$LOG_DIR/client-sm-d8.stderr.log"
  cat "$LOG_DIR/client-sm-d8.stdout.log"
  fetch_evidence play-a-sm-d8 "$HTTP_A"
  fetch_evidence play-b-sm-d8 "$HTTP_B"
  fetch_evidence session-a-sm-d8 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d8-evidence.json" "$LOG_DIR/play-b-sm-d8-evidence.json" "$LOG_DIR/session-a-sm-d8-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d8-reconnect"

def items(snapshot, marker, actor_id):
    return [item for item in snapshot["entries"]
            if item["marker"] == marker and item["actor_id"] == actor_id]

def has(snapshot, marker, actor_id, value=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor_id
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert len(items(play_a, "ActorEnsured", actor)) >= 2
assert len(items(play_a, "EntryActorSlowPing", actor)) == 1
assert has(play_a, "EntryActorSlowPing", actor, "before-disconnect:1")
assert has(play_a, "EntryActorPing", actor, "after-reconnect:2")
assert len(items(session_a, "StreamBound", actor)) == 2
assert len(items(session_a, "StreamUnbound", actor)) >= 1
assert not any(item["actor_id"] == actor for item in play_b["entries"])
print("scenario SM-D8 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D9" || "$SCENARIO" == "sm-d9" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d9 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d9" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d9.stdout.log" 2>"$LOG_DIR/client-sm-d9.stderr.log"
  cat "$LOG_DIR/client-sm-d9.stdout.log"
  fetch_evidence play-a-sm-d9 "$HTTP_A"
  fetch_evidence play-b-sm-d9 "$HTTP_B"
  fetch_evidence session-a-sm-d9 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d9-evidence.json" "$LOG_DIR/play-b-sm-d9-evidence.json" "$LOG_DIR/session-a-sm-d9-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d9"

def has(snapshot, marker, actor_id, value=None):
    return any(item["marker"] == marker
               and item["actor_id"] == actor_id
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "ActorEnsured", actor, "SM-D9 Observer")
assert has(play_a, "EntryActorPing", actor, "observer-1:1")
assert has(play_a, "EntryActorPing", actor, "observer-2:2")
assert has(session_a, "StreamBound", actor)
assert not any(item["actor_id"] == actor for item in play_b["entries"])
print("scenario SM-D9 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D11" || "$SCENARIO" == "sm-d11" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SESSION_HTTP_ENDPOINT="$HTTP_SESSION_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d11 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d11" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d11.stdout.log" 2>"$LOG_DIR/client-sm-d11.stderr.log"
  cat "$LOG_DIR/client-sm-d11.stdout.log"
  fetch_evidence play-a-sm-d11 "$HTTP_A"
  fetch_evidence play-b-sm-d11 "$HTTP_B"
  fetch_evidence session-a-sm-d11 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d11-evidence.json" "$LOG_DIR/play-b-sm-d11-evidence.json" "$LOG_DIR/session-a-sm-d11-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d11"

def has(snapshot, marker, actor_id=None, value=None):
    return any(item["marker"] == marker
               and (actor_id is None or item["actor_id"] == actor_id)
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "ActorEnsured", actor, "SM-D11 Stream Channel")
assert has(play_a, "EntryActorPing", actor, "stream-side:1")
assert has(play_a, "ChannelEcho", value="channel-side")
assert has(session_a, "StreamBound", actor)
assert not any(item["actor_id"] == actor for item in play_b["entries"])
print("scenario SM-D11 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D12" || "$SCENARIO" == "sm-d12" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  start_session session-b "$ROUTE_SESSION_B" "$SPOT_SESSION_B" "$PUB_SESSION_B" "$STREAM_B" "$HTTP_SESSION_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT="$STREAM_B" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d12 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d12" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d12.stdout.log" 2>"$LOG_DIR/client-sm-d12.stderr.log"
  cat "$LOG_DIR/client-sm-d12.stdout.log"
  fetch_evidence play-a-sm-d12 "$HTTP_A"
  fetch_evidence play-b-sm-d12 "$HTTP_B"
  fetch_evidence session-a-sm-d12 "$HTTP_SESSION_A"
  fetch_evidence session-b-sm-d12 "$HTTP_SESSION_B"
  python3 - "$LOG_DIR/play-a-sm-d12-evidence.json" "$LOG_DIR/play-b-sm-d12-evidence.json" "$LOG_DIR/session-a-sm-d12-evidence.json" "$LOG_DIR/session-b-sm-d12-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
session_b = json.load(open(sys.argv[4], encoding="utf-8"))
actor = "actor-sm-d12-transfer"

def has(snapshot, marker, actor_id=None, value=None):
    return any(item["marker"] == marker
               and (actor_id is None or item["actor_id"] == actor_id)
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "ActorEnsured", actor, "SM-D12 Transfer")
assert has(play_a, "ActorJoinedCallback", actor)
assert has(play_a, "StateMutated", actor, "11")
assert has(play_a, "StateMutated", actor, "16")
assert has(play_a, "ActorPushedSession", actor, "after-transfer")
assert has(session_a, "StreamBound", actor)
assert has(session_b, "StreamBound", actor)
assert not any(item["actor_id"] == actor for item in play_b["entries"])
print("scenario SM-D12 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-D13" || "$SCENARIO" == "sm-d13" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-d13 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-d13" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-d13.stdout.log" 2>"$LOG_DIR/client-sm-d13.stderr.log"
  cat "$LOG_DIR/client-sm-d13.stdout.log"
  fetch_evidence play-a-sm-d13 "$HTTP_A"
  fetch_evidence play-b-sm-d13 "$HTTP_B"
  fetch_evidence session-a-sm-d13 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-d13-evidence.json" "$LOG_DIR/play-b-sm-d13-evidence.json" "$LOG_DIR/session-a-sm-d13-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
actor = "actor-sm-d13"

def has(snapshot, marker, actor_id=None, value=None):
    return any(item["marker"] == marker
               and (actor_id is None or item["actor_id"] == actor_id)
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "ActorEnsured", actor, "SM-D13 Heartbeat")
assert has(play_a, "EntryActorPing", actor, "heartbeat:1")
assert has(session_a, "StreamBound", actor)
assert not any(item["actor_id"] == actor for item in play_b["entries"])
print("scenario SM-D13 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-E1" || "$SCENARIO" == "sm-e1" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-e1 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-e1" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-e1.stdout.log" 2>"$LOG_DIR/client-sm-e1.stderr.log"
  cat "$LOG_DIR/client-sm-e1.stdout.log"
  for _ in $(seq 1 100); do
    if grep -q "surface=spot_route.*reason=handler_missing.*action=reply_error.*packet=MissingSpotReq" \
      "$LOG_DIR/play-b.stderr.log" \
      && grep -q "surface=spot_route.*reason=handler_missing.*action=drop.*packet=MissingSpotSend" \
        "$LOG_DIR/play-b.stderr.log"; then
      break
    fi
    sleep 0.05
  done
  grep -q "surface=spot_route.*reason=handler_missing.*action=reply_error.*packet=MissingSpotReq" \
    "$LOG_DIR/play-b.stderr.log"
  grep -q "surface=spot_route.*reason=handler_missing.*action=drop.*packet=MissingSpotSend" \
    "$LOG_DIR/play-b.stderr.log"
  fetch_evidence play-a-sm-e1 "$HTTP_A"
  fetch_evidence play-b-sm-e1 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-e1-evidence.json" "$LOG_DIR/play-b-sm-e1-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))

def has(snapshot, marker, spot_rid=None):
    return any(item["marker"] == marker
               and (spot_rid is None or item["spot_rid"] == spot_rid)
               for item in snapshot["entries"])

assert has(play_b, "SpotInitialized", "user:play-b:sm-e1-missing")
assert not any(item["spot_rid"] == "user:play-b:sm-e1-missing" for item in play_a["entries"])
print("scenario SM-E1 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-G2" || "$SCENARIO" == "sm-g2" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-g2 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-g2" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-g2.stdout.log" 2>"$LOG_DIR/client-sm-g2.stderr.log"
  cat "$LOG_DIR/client-sm-g2.stdout.log"
  fetch_evidence play-a-sm-g2 "$HTTP_A"
  fetch_evidence play-b-sm-g2 "$HTTP_B"
  python3 - "$LOG_DIR/play-a-sm-g2-evidence.json" "$LOG_DIR/play-b-sm-g2-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))

first_spot = "user:play-a:sm-g2-owner"
remapped_spot = "user:play-b:sm-g2-owner"

def has(snapshot, marker, spot_rid, value=None):
    return any(item["marker"] == marker
               and item["spot_rid"] == spot_rid
               and (value is None or item["value"] == value)
               for item in snapshot["entries"])

assert has(play_a, "SpotInitialized", first_spot)
assert has(play_b, "SpotInitialized", remapped_spot)
assert has(play_a, "SpotToSpotRequest", first_spot, "sm-g2-before-remap")
assert has(play_b, "SpotToSpotRequest", remapped_spot, "sm-g2-after-remap")
assert not any(item["spot_rid"] == remapped_spot for item in play_a["entries"])
assert not any(item["spot_rid"] == first_spot for item in play_b["entries"])
print("scenario SM-G2 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-G3" || "$SCENARIO" == "sm-g3" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-g3 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-g3" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-g3.stdout.log" 2>"$LOG_DIR/client-sm-g3.stderr.log"
  cat "$LOG_DIR/client-sm-g3.stdout.log"
  fetch_evidence play-a-sm-g3 "$HTTP_A"
  fetch_evidence session-a-sm-g3 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-g3-evidence.json" "$LOG_DIR/session-a-sm-g3-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
session_a = json.load(open(sys.argv[2], encoding="utf-8"))
spot = "user:play-a:sm-g3-concurrent"
actors = ["actor-sm-g3-0", "actor-sm-g3-1"]

def count(snapshot, marker, actor_id, spot_rid=None):
    return sum(1 for item in snapshot["entries"]
               if item["marker"] == marker
               and item["actor_id"] == actor_id
               and (spot_rid is None or item["spot_rid"] == spot_rid))

for actor in actors:
    assert count(play_a, "ActorJoined", actor, spot) == 1
    assert count(play_a, "ActorLeft", actor, spot) == 1
    assert count(session_a, "StreamBound", actor) == 1
print("scenario SM-G3 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "SM-G4" || "$SCENARIO" == "sm-g4" ]]; then
  start_registry
  start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
  start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
  start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
  sleep 20
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
  ZLINK_CPP_E2E_SCENARIO_MODE=sm-g4 \
  ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_CLIENT_RID="client-sm-g4" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-sm-g4.stdout.log" 2>"$LOG_DIR/client-sm-g4.stderr.log"
  cat "$LOG_DIR/client-sm-g4.stdout.log"
  fetch_evidence play-a-sm-g4 "$HTTP_A"
  fetch_evidence session-a-sm-g4 "$HTTP_SESSION_A"
  python3 - "$LOG_DIR/play-a-sm-g4-evidence.json" "$LOG_DIR/session-a-sm-g4-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
session_a = json.load(open(sys.argv[2], encoding="utf-8"))
actors = [f"actor-sm-g4-{index}" for index in range(6)]

def count(snapshot, marker, actor_id, value=None):
    return sum(1 for item in snapshot["entries"]
               if item["marker"] == marker
               and item["actor_id"] == actor_id
               and (value is None or item["value"] == value))

for index, actor in enumerate(actors):
    assert count(session_a, "StreamBound", actor) == 1
    assert count(play_a, "EntryActorPushedSession", actor, f"push-{index}") == 1
print("scenario SM-G4 evidence passed")
PY
  echo "spot-service e2e result=passed"
  exit 0
fi

start_registry
start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
sleep 20

run_base_client base client

start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
start_session session-b "$ROUTE_SESSION_B" "$SPOT_SESSION_B" "$PUB_SESSION_B" "$STREAM_B" "$HTTP_SESSION_B"
sleep 20

ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT="$STREAM_B" \
ZLINK_CPP_E2E_SCENARIO_MODE=stream \
ZLINK_CPP_E2E_CLIENT_RID="client-stream" \
ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/stream-client.stdout.log" 2>"$LOG_DIR/stream-client.stderr.log"

cat "$LOG_DIR/stream-client.stdout.log"
sleep 1
fetch_evidence play-a "$HTTP_A"
fetch_evidence play-b "$HTTP_B"
fetch_evidence session-a "$HTTP_SESSION_A"
fetch_evidence session-b "$HTTP_SESSION_B"
grep -q "surface=spot_actor.*reason=handler_missing.*action=reply_error.*packet=MissingActorPacket" \
  "$LOG_DIR/play-a.stderr.log"

python3 - "$LOG_DIR/play-a-evidence.json" "$LOG_DIR/play-b-evidence.json" "$LOG_DIR/session-a-evidence.json" "$LOG_DIR/session-b-evidence.json" <<'PY'
import json
import sys

play_a = json.load(open(sys.argv[1], encoding="utf-8"))
play_b = json.load(open(sys.argv[2], encoding="utf-8"))
session_a = json.load(open(sys.argv[3], encoding="utf-8"))
session_b = json.load(open(sys.argv[4], encoding="utf-8"))

def has(snapshot, marker, actor=None):
    for entry in snapshot["entries"]:
        if entry["marker"] != marker:
            continue
        if actor is not None and entry["actor_id"] != actor:
            continue
        return True
    return False

def has_value(snapshot, marker, actor, value):
    for entry in snapshot["entries"]:
        if entry["marker"] == marker and entry["actor_id"] == actor and entry["value"] == value:
            return True
    return False

def has_marker_value(snapshot, marker, value):
    for entry in snapshot["entries"]:
        if entry["marker"] == marker and entry["value"] == value:
            return True
    return False

def marker_index(snapshot, marker, actor):
    for index, entry in enumerate(snapshot["entries"]):
        if entry["marker"] == marker and entry["actor_id"] == actor:
            return index
    raise AssertionError(f"missing marker {marker} for {actor}")

def assert_order(snapshot, actor, markers):
    indices = [marker_index(snapshot, marker, actor) for marker in markers]
    assert indices == sorted(indices), (actor, markers, indices)

def count(snapshot, marker, actor=None):
    total = 0
    for entry in snapshot["entries"]:
        if entry["marker"] != marker:
            continue
        if actor is not None and entry["actor_id"] != actor:
            continue
        total += 1
    return total

assert has(play_a, "ActorEnsured", "alice")
assert has(play_a, "EntryJoin", "alice")
assert has(play_a, "StateMutated", "alice")
assert has(play_a, "SpotTypeMismatch", "alice")
assert has(play_a, "SpotClosing")
assert has(play_a, "SpotLifecycleClosed")
assert has(play_a, "ActorLeaveRequested", "alice")
assert has(play_a, "ActorDestroyed", "alice")
assert has_value(play_a, "WorkerStarted", "alice-2", "7")
assert has_value(play_a, "WorkerCompleted", "alice-2", "25")
assert has(play_a, "SpotOutbound", "alice-2")
assert has_value(play_a, "SpotToSpotOutbound", "alice-2", "spot-to-spot:reply")
assert has(play_a, "MeshEventReceived")
assert has_marker_value(play_a, "MeshEventReceived", "evt-spot-to-spot:alice-2:spot-to-spot")
assert has_marker_value(play_a, "MeshEventReceived", "evt-publisher-client:publish-only")
assert has_value(play_b, "SpotToSpotRequest", "alice-2", "spot-to-spot")
assert has_value(play_b, "SpotToSpotCommand", "alice-2", "spot-to-spot:command")
assert has_marker_value(play_b, "MeshEventReceived", "evt-spot-to-spot:alice-2:spot-to-spot")
assert has_marker_value(play_b, "MeshEventReceived", "evt-publisher-client:publish-only")
assert has_value(play_a, "ActorPushedSession", "stream-local", "stream-local-push")
assert has_value(play_a, "ActorPushedSession", "stream-multi-a", "stream-multi-a-push")
assert has_value(play_a, "ActorPushedSession", "stream-push-d6", "stream-push-d6-value")
assert has(play_a, "EntryJoin", "stream-auth-d7")
assert has_value(play_a, "StateMutated", "stream-auth-d7", "7")
assert has(play_a, "ActorDisconnected", "stream-disconnect-d5-notified")
assert count(play_a, "ActorDisconnected", "stream-disconnect-d5-muted") == 0
assert has_value(play_a, "StateMutated", "stream-reconnect-d12", "11")
assert has_value(play_a, "StateMutated", "stream-reconnect-d12", "16")
assert has_value(play_a, "ActorPushedSession", "stream-reconnect-d12", "stream-reconnect-d12-push")
assert has(play_b, "ActorEnsured", "bob")
assert has(play_b, "EntryJoin", "bob")
assert has(play_b, "StateMutated", "bob")
assert has_value(play_b, "ActorPushedSession", "stream-remote", "stream-remote-push")
assert has_value(play_b, "ActorPushedSession", "stream-multi-b", "stream-multi-b-push")
assert_order(play_a, "alice",
             ["ActorCreated", "EntryJoin", "ActorJoined", "ActorJoinedCallback",
              "StateMutated"])
assert_order(play_b, "bob",
             ["ActorCreated", "EntryJoin", "ActorJoined", "ActorJoinedCallback",
              "StateMutated"])
assert has(session_a, "StreamBound", "stream-local")
assert has(session_a, "StreamBound", "stream-remote")
assert has(session_a, "StreamBound", "stream-multi-a")
assert has(session_a, "StreamBound", "stream-multi-b")
assert has(session_a, "StreamBound", "stream-push-d6")
assert has(session_a, "StreamBound", "stream-auth-d7")
assert has(session_a, "StreamAuthFailed", "stream-auth-d7")
assert has(session_a, "StreamBound", "stream-disconnect-d5-notified")
assert has(session_a, "StreamBound", "stream-disconnect-d5-muted")
assert has(session_a, "StreamDisconnectNotified", "stream-disconnect-d5-notified")
assert has(session_a, "StreamBound", "stream-reconnect-d12")
assert has(session_b, "StreamBound", "stream-reconnect-d12")
print("scenario SM-B7 passed")
print("spot-service evidence result=passed")
PY

if [[ -n "$PLAY_A_PID" ]] && kill -0 "$PLAY_A_PID" >/dev/null 2>&1; then
  kill -9 "$PLAY_A_PID" >/dev/null 2>&1 || true
  wait "$PLAY_A_PID" >/dev/null 2>&1 || true
fi
wait_port_closed play-a-route "$ROUTE_A"
wait_port_closed play-a-spot "$SPOT_A"
wait_port_closed play-a-http "$HTTP_A"
if [[ -n "$SESSION_A_PID" ]] && kill -0 "$SESSION_A_PID" >/dev/null 2>&1; then
  kill -9 "$SESSION_A_PID" >/dev/null 2>&1 || true
  wait "$SESSION_A_PID" >/dev/null 2>&1 || true
fi
wait_port_closed session-a-route "$ROUTE_SESSION_A"
wait_port_closed session-a-spot "$SPOT_SESSION_A"
wait_port_closed session-a-stream "$STREAM_A"
wait_port_closed session-a-http "$HTTP_SESSION_A"
if [[ -n "$PLAY_B_PID" ]] && kill -0 "$PLAY_B_PID" >/dev/null 2>&1; then
  kill -9 "$PLAY_B_PID" >/dev/null 2>&1 || true
  wait "$PLAY_B_PID" >/dev/null 2>&1 || true
fi
wait_port_closed play-b-route "$ROUTE_B"
wait_port_closed play-b-spot "$SPOT_B"
wait_port_closed play-b-http "$HTTP_B"
start_play play-a "$ROUTE_A" "$SPOT_A" "$PUB_A" "$HTTP_A"
start_play play-b "$ROUTE_B" "$SPOT_B" "$PUB_B" "$HTTP_B"
start_session session-a "$ROUTE_SESSION_A" "$SPOT_SESSION_A" "$PUB_SESSION_A" "$STREAM_A" "$HTTP_SESSION_A"
sleep 20

CRASH_READY="$LOG_DIR/sm-g1-ready"
CRASH_GO="$LOG_DIR/sm-g1-go"
CRASH_OBSERVED="$LOG_DIR/sm-g1-observed"

ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
ZLINK_CPP_E2E_SCENARIO_MODE=crash-setup \
ZLINK_CPP_E2E_CLIENT_RID="client-crash-setup" \
ZLINK_CPP_E2E_CRASH_READY_FILE="$CRASH_READY" \
ZLINK_CPP_E2E_CRASH_GO_FILE="$CRASH_GO" \
ZLINK_CPP_E2E_CRASH_OBSERVED_FILE="$CRASH_OBSERVED" \
ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/crash-client.stdout.log" 2>"$LOG_DIR/crash-client.stderr.log" &
CRASH_CLIENT_PID="$!"
PIDS+=("$CRASH_CLIENT_PID")

wait_file "SM-G1 ready" "$CRASH_READY"
kill -9 "$PLAY_A_PID" >/dev/null 2>&1 || true
wait "$PLAY_A_PID" >/dev/null 2>&1 || true
touch "$CRASH_GO"
wait_file "SM-G1 crash observed" "$CRASH_OBSERVED"
wait "$CRASH_CLIENT_PID"
cat "$LOG_DIR/crash-client.stdout.log"

ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_A" \
ZLINK_CPP_E2E_SCENARIO_MODE=crash-recover \
ZLINK_CPP_E2E_CLIENT_RID="client-crash-recover" \
ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/crash-recover-client.stdout.log" 2>"$LOG_DIR/crash-recover-client.stderr.log"
cat "$LOG_DIR/crash-recover-client.stdout.log"

fetch_evidence play-b-crash "$HTTP_B"
fetch_evidence session-a-crash "$HTTP_SESSION_A"

python3 - "$LOG_DIR/play-b-crash-evidence.json" "$LOG_DIR/session-a-crash-evidence.json" <<'PY'
import json
import sys

play_b = json.load(open(sys.argv[1], encoding="utf-8"))
session_a = json.load(open(sys.argv[2], encoding="utf-8"))

def has(snapshot, marker, actor=None):
    return any(entry["marker"] == marker and (actor is None or entry["actor_id"] == actor)
               for entry in snapshot["entries"])

def has_value(snapshot, marker, actor, value):
    return any(entry["marker"] == marker and entry["actor_id"] == actor and entry["value"] == value
               for entry in snapshot["entries"])

assert has_value(play_b, "StateMutated", "crash-g1-play-b", "42")
assert has_value(play_b, "StateMutated", "crash-g1-play-b", "49")
assert has(session_a, "StreamBound", "crash-g1-play-a")
assert has(session_a, "StreamBound", "crash-g1-play-b")
print("spot-service crash evidence result=passed")
PY

echo "spot-service e2e result=passed"
