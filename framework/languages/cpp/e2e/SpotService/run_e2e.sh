#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER ROUTE_A ROUTE_B ROUTE_SESSION_A ROUTE_SESSION_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_SESSION_A SPOT_SESSION_B SPOT_CLIENT PUB_A PUB_B PUB_SESSION_A PUB_SESSION_B PUB_CLIENT PUBLISHER_CLIENT API_CLIENT STREAM_A STREAM_B HTTP_A HTTP_B HTTP_SESSION_A HTTP_SESSION_B <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(25):
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
  zlink_cpp_e2e_spot_service_server \
  zlink_cpp_e2e_spot_service_client >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_spot_service_server"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_spot_service_client"
PIDS=()
PLAY_A_PID=""
PLAY_B_PID=""
SESSION_A_PID=""

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
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot" \
  ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$pubsub" \
  ZLINK_CPP_E2E_API_PEER_ENDPOINT="$API_CLIENT" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
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
  ZLINK_CPP_E2E_ROLE=session \
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
    "$SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
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
sleep 20

ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE_CLIENT" \
ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_CLIENT" \
ZLINK_CPP_E2E_PUBSUB_ENDPOINT="$PUB_CLIENT" \
ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER_CLIENT" \
ZLINK_CPP_E2E_API_ENDPOINT="$API_CLIENT" \
ZLINK_CPP_E2E_SCENARIO_MODE=base \
ZLINK_CPP_E2E_CLIENT_RID="client-base" \
ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"

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
grep -q "surface=spot_actor.*reason=handler_missing.*packet=MissingActorPacket" \
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
