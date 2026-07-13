#!/usr/bin/env bash
# Config 11 — ObservabilityOps (metrics · flow correlation · drain).
# Implemented scenario subset is asserted; the remaining OBS ids are reported
# as PENDING with the blocking runtime work (see feature-map.ko.md).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="$(cd "$ROOT_DIR/../.." && pwd)"
source "$ROOT_DIR/../redis-common.sh"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$FRAMEWORK_DIR/build-redis-vcpkg}"
SCENARIO="${1:-all}"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

read -r PLAY_A_HTTP PLAY_B_HTTP PLAY_A_ROUTE PLAY_B_ROUTE PLAY_A_SPOT_ROUTER \
  PLAY_B_SPOT_ROUTER PLAY_A_SPOT_PUB PLAY_B_SPOT_PUB STREAM_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sockets = []
ports = []
for _ in range(9):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
values = [f"http://127.0.0.1:{ports[0]}", f"http://127.0.0.1:{ports[1]}"]
values += [f"tcp://127.0.0.1:{p}" for p in ports[2:9]]
print(" ".join(values))
for s in sockets:
    s.close()
PY
)"

cmake -S "$FRAMEWORK_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_observability_ops_server \
  zlink_cpp_e2e_observability_ops_trigger >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_observability_ops_server"
TRIGGER="$BUILD_DIR/zlink_cpp_e2e_observability_ops_trigger"

zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
  "zlink-redis-cpp-e2e-observabilityops" "redis:7-alpine"
REDIS_ENDPOINT="127.0.0.1:${redis_port}"
REDIS_KEY_PREFIX="zlink:cpp:observability-ops:${RUN_ID}"
PIDS=()

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then kill "$pid" >/dev/null 2>&1 || true; fi
  done
  for pid in "${PIDS[@]:-}"; do wait "$pid" 2>/dev/null || true; done
  docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  if [[ "$code" -ne 0 ]]; then echo "ObservabilityOps failed. Logs: $LOG_DIR" >&2; fi
}
trap cleanup EXIT

launch_role() {
  local role="$1" http="$2" route="$3" peer_route="$4" spot_router="$5" spot_pub="$6" stream="$7"
  env \
    ZLINK_CPP_E2E_ROLE="$role" \
    ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
    ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
    ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
    ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
    ZLINK_CPP_E2E_PEER_ROUTE_ENDPOINT="$peer_route" \
    ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot_router" \
    ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$spot_pub" \
    ZLINK_CPP_E2E_STREAM_ENDPOINT="$stream" \
    ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/$role.stdout.log" 2>"$LOG_DIR/$role.stderr.log" &
  PIDS+=("$!")
}

wait_health() {
  local url="$1"
  for _ in $(seq 1 300); do
    if curl -fsS --max-time 1 "$url/health" >/dev/null 2>&1; then return 0; fi
    sleep 0.1
  done
  echo "Timed out waiting for $url" >&2
  return 1
}

# play-a hosts the STREAM session gateway; play-b hosts the target room.
launch_role play-a "$PLAY_A_HTTP" "$PLAY_A_ROUTE" "$PLAY_B_ROUTE" \
  "$PLAY_A_SPOT_ROUTER" "$PLAY_A_SPOT_PUB" "$STREAM_ENDPOINT"
launch_role play-b "$PLAY_B_HTTP" "$PLAY_B_ROUTE" "$PLAY_A_ROUTE" \
  "$PLAY_B_SPOT_ROUTER" "$PLAY_B_SPOT_PUB" ""
wait_health "$PLAY_A_HTTP"
wait_health "$PLAY_B_HTTP"
sleep 2

ensure() {
  local condition_result="$1" message="$2"
  if [[ "$condition_result" != "0" ]]; then
    echo "ensure failed: $message" >&2
    exit 1
  fi
}

SPOT_RID="obs-room-1"
curl -fsS -X POST "$PLAY_B_HTTP/spot/create" -H 'Content-Type: application/json' \
  -d "{\"spotRid\":\"$SPOT_RID\"}" >"$LOG_DIR/create-room.json"
python3 - "$LOG_DIR/create-room.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["state"] in ("created", "existing"), body
PY
sleep 2

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "flow" ]]; then
  # OBS-A1 — one connector-generated flow id threads
  # trigger -> play-a session inbound -> play-b room-spot dispatch.
  env ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_ENDPOINT" ZLINK_CPP_E2E_SPOT_RID="$SPOT_RID" \
    "$TRIGGER" flow >"$LOG_DIR/trigger-flow.log" 2>&1
  python3 - "$LOG_DIR/play-a-flow.log" "$LOG_DIR/play-b-flow.log" <<'PY'
import re, sys
def flows(path, needle=None):
    ids = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if needle and needle not in line:
            continue
        m = re.search(r"flow=([0-9a-f-]{36})", line)
        if m:
            ids.append(m.group(1))
    return ids
session_ids = set(flows(sys.argv[1]))
spot_ids = set(flows(sys.argv[2]))
shared = session_ids & spot_ids
assert shared, f"no shared flow id between session and spot logs: {session_ids} / {spot_ids}"
# OBS-A1: the shared flow was created by the connector (origin=application).
origin_ok = False
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if any(f"flow={fid}" in line for fid in shared) and "origin=application" in line:
        origin_ok = True
        break
assert origin_ok, "shared flow line lacks origin=application"
print("OBS-A1 PASS (flow threads connector->session->room-spot)")
PY

  # OBS-A2 — the dispatch error line carries flow= too.
  env ZLINK_CPP_E2E_STREAM_ENDPOINT="$STREAM_ENDPOINT" ZLINK_CPP_E2E_SPOT_RID="$SPOT_RID" \
    "$TRIGGER" error >"$LOG_DIR/trigger-error.log" 2>&1
  python3 - "$LOG_DIR/play-a-flow.log" <<'PY'
import re, sys
error_flow = None
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if "outcome=error" in line or "zlink dispatch error" in line:
        m = re.search(r"flow=([0-9a-f-]{36})", line)
        if m:
            error_flow = m.group(1)
assert error_flow, "no error line with flow= found"
print("OBS-A2 PASS (error line carries flow=)")
PY
fi

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "metrics" ]]; then
  # OBS-B subset — spot.created/spot.count with kind label and
  # channel.request.duration samples appear in the evidence collector.
  curl -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.metrics.evidence.json"
  curl -fsS "$PLAY_A_HTTP/evidence" >"$LOG_DIR/play-a.metrics.evidence.json"
  python3 - "$LOG_DIR/play-b.metrics.evidence.json" "$LOG_DIR/play-a.metrics.evidence.json" <<'PY'
import json, sys
play_b = json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
created = [m for m in play_b if m["name"] == "zlink.spot.created" and m["kind"] == "counter"]
count = [m for m in play_b if m["name"] == "zlink.spot.count" and m["kind"] == "updown"]
assert created and created[0]["tags"].get("kind") in ("user", "entry"), created
assert count, "zlink.spot.count missing"
play_a = json.load(open(sys.argv[2], encoding="utf-8"))["metrics"]
durations = [m for m in play_a if m["name"] == "zlink.channel.request.duration"]
assert durations and durations[0]["kind"] == "histogram" and durations[0]["unit"] == "s", durations
# OBS-B1 subset — server-side session counters follow accepts/closes.
opened = [m for m in play_a if m["name"] == "zlink.stream.connections.opened"]
closed = [m for m in play_a if m["name"] == "zlink.stream.connections.closed"]
active = [m for m in play_a if m["name"] == "zlink.stream.connections.active"]
assert len(opened) >= 2 and len(active) >= 2, (opened, active)
assert closed and all(
    m["tags"].get("close_reason") in ("client_close", "idle_timeout", "heartbeat_timeout",
                                      "server_drain", "protocol_error", "transport_error")
    for m in closed), closed
net_active = sum(m["value"] for m in active)
assert net_active == 0, f"active sessions should net to zero after triggers: {net_active}"
# OBS-B2 subset — spot serial queue depth/wait samples with kind label.
queue_depth = [m for m in play_b if m["name"] == "zlink.spot.queue.depth"]
queue_wait = [m for m in play_b if m["name"] == "zlink.spot.queue.wait.duration"]
assert queue_depth and all(m["tags"].get("kind") in ("user", "entry") for m in queue_depth), queue_depth
assert queue_wait and queue_wait[0]["kind"] == "histogram" and queue_wait[0]["unit"] == "s", queue_wait
assert sum(m["value"] for m in queue_depth) == 0, "queue depth should net to zero"
forbidden = {"correlation_id", "flow_id", "actor_id", "spot_rid"}
for sample in play_a + play_b:
    assert not (forbidden & set(sample["tags"])), f"high-cardinality label: {sample}"
print("OBS-B(subset) PASS (spot/queue/channel/stream instruments, closed labels)")
PY
fi

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "drain" ]]; then
  # OBS-C1 subset — draining marker keeps the peer row (draining=true),
  # readiness flips, new spot creation is rejected, drain events observed.
  curl -fsS -X POST "$PLAY_B_HTTP/drain" -H 'Content-Type: application/json' -d '{"deadlineMs":10000}' >/dev/null
  for _ in $(seq 1 100); do
    curl -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.drain.evidence.json" || true
    if python3 - "$LOG_DIR/play-b.drain.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
ok = "draining" in states and body["ready"] is False
raise SystemExit(0 if ok else 1)
PY
    then break; fi
    sleep 0.2
  done
  python3 - "$LOG_DIR/play-b.drain.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
assert "draining" in states, states
assert body["ready"] is False, "play-b still ready after drain"
# The draining marker window can be brief when no handoff work exists: either
# the marked row is still visible, or the owner cleanup already removed
# play-b's rows (post-terminal). Both prove the marker+cleanup path ran.
play_b_hex = "play-b".encode().hex()
play_b_rows = [row for row in body["peerRows"] if row["nodeRid"] == play_b_hex]
draining_rows = [row for row in play_b_rows if row["draining"]]
# rows already removed => owner cleanup is underway; the terminal `drained`
# state is asserted separately on the final snapshot below.
cleaned_up = not play_b_rows
assert draining_rows or cleaned_up, f"neither draining marker nor cleanup: {body['peerRows']}"
assert all(event["source"] == "drain" for event in body["drainEvents"]), body["drainEvents"]
print("OBS-C1(subset) PASS (marker/cleanup + readiness flip + drain events)")
PY
  # New spot creation on the draining node is rejected (RequestRejected -> HTTP error).
  if curl -fsS -X POST "$PLAY_B_HTTP/spot/create" -H 'Content-Type: application/json' -d '{"spotRid":"obs-room-rejected"}' \
      >"$LOG_DIR/create-while-draining.json" 2>/dev/null; then
    python3 - "$LOG_DIR/create-while-draining.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
raise SystemExit(0 if body.get("state") == "rejected" else 1)
PY
  fi
  echo "OBS-C1 create-rejection PASS"
  # Existing peer (play-a) stays ready and serving.
  curl -fsS "$PLAY_A_HTTP/evidence" >"$LOG_DIR/play-a.drain.evidence.json"
  python3 - "$LOG_DIR/play-a.drain.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["ready"] is True, "play-a lost readiness"
print("OBS-C1 peer-isolation PASS (play-a stays ready)")
PY
  # Terminal result within the deadline (no in-flight work left).
  for _ in $(seq 1 150); do
    curl -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.drain-final.evidence.json" || break
    if python3 - "$LOG_DIR/play-b.drain-final.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
raise SystemExit(0 if ("drained" in states or "force_stopping" in states) else 1)
PY
    then break; fi
    sleep 0.2
  done
  python3 - "$LOG_DIR/play-b.drain-final.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
assert "drained" in states, f"drain did not reach a terminal state: {states}"
print("OBS-C1 terminal PASS (drained)")
PY
fi

cat <<'EOF'
PENDING (blocked on remaining G2 runtime work, see feature-map.ko.md):
  OBS-A3 (off-node propagation topology), OBS-A4 (publish fan-out tree/timer origin),
  OBS-B1 (stream.connections instruments), OBS-B2 (spot.queue/actor.transfer instruments),
  OBS-B3 (fanout/lease instruments), OBS-B4 (inactive-cost),
  OBS-C2 (actor handoff), OBS-C3 (release-and-recreate), OBS-C4 (forced session notify),
  OBS-C5 (rolling/zero-target)
EOF
echo "ObservabilityOps scenario=$SCENARIO PASS (implemented subset)"
