#!/usr/bin/env bash
# Config 11 — ObservabilityOps (metrics · flow correlation · drain).
# Scenario subsets are asserted; incomplete OBS ids remain deferred with their
# blocking work recorded in feature-map.ko.md.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="$(cd "$ROOT_DIR/../.." && pwd)"
source "$ROOT_DIR/../redis-common.sh"
BUILD_DIR="$FRAMEWORK_DIR/build"
SCENARIO="${1:-all}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
EVIDENCE_POLL_SECONDS=0.2

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
CONFIG_DIR="$LOG_DIR/config"
mkdir -p "$LOG_DIR"
mkdir -p "$CONFIG_DIR"
echo "log_dir=$LOG_DIR"

assign_ports() {
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
}
assign_ports

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
  docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  rm -rf "$CONFIG_DIR"
  if [[ "$code" -ne 0 ]]; then echo "ObservabilityOps failed. Logs: $LOG_DIR" >&2; fi
}
trap cleanup EXIT

launch_role() {
  local role="$1" http="$2" route="$3" peer_route="$4" spot_router="$5" spot_pub="$6" stream="$7"
  local log_dir="${8:-$LOG_DIR}"
  local trace_mode="${9:-key_transitions}"
  local metrics="${10:-on}"
  local drain_policy="${11:-drain_natural}"
  local room_timer="${12:-off}"
  local config_path="$CONFIG_DIR/$role-$(date +%s%N).json"
  mkdir -p "$log_dir"
  python3 - "$config_path" "$role" "$REDIS_ENDPOINT" "$REDIS_KEY_PREFIX" \
    "$http" "$route" "$peer_route" "$spot_router" "$spot_pub" "$stream" \
    "$log_dir" "$trace_mode" "$metrics" "$drain_policy" "$room_timer" <<'PY'
import json
import os
import stat
import sys

(path, role, redis_endpoint, redis_key_prefix, http, route, peer_route,
 spot_router, spot_pub, stream, log_dir, trace_mode, metrics, drain_policy,
 room_timer) = sys.argv[1:]
with open(path, "w", encoding="utf-8") as file:
    json.dump({"e2e": {"role": role,
        "redis": {"endpoint": redis_endpoint, "keyPrefix": redis_key_prefix},
        "httpEndpoint": http, "routeEndpoint": route,
        "peerRouteEndpoint": peer_route, "spotRouterEndpoint": spot_router,
        "spotPubEndpoint": spot_pub, "streamEndpoint": stream,
        "logDir": log_dir, "traceMode": trace_mode, "metrics": metrics,
        "drainPolicy": drain_policy, "roomTimer": room_timer}}, file, indent=2)
os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
PY
  "$SERVER" --config="$config_path" \
    >"$log_dir/$role.stdout.log" 2>"$log_dir/$role.stderr.log" &
  PIDS+=("$!")
}

stop_roles() {
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then kill "$pid" >/dev/null 2>&1 || true; fi
  done
  for pid in "${PIDS[@]:-}"; do wait "$pid" 2>/dev/null || true; done
  PIDS=()
}

curl_local() {
  curl --max-time "$HTTP_PROBE_TIMEOUT_SECONDS" \
    --connect-timeout "$HTTP_PROBE_TIMEOUT_SECONDS" "$@"
}

wait_health() {
  local url="$1"
  local attempts
  attempts="$(python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys
print(max(1, math.ceil(float(sys.argv[1]) / float(sys.argv[2]))))
PY
)"
  for _ in $(seq 1 "$attempts"); do
    if curl_local -fsS "$url/health" >/dev/null 2>&1; then return 0; fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $url" >&2
  return 1
}

# play-a hosts the STREAM session gateway (and the timer-enabled subscriber
# room for OBS-A4); play-b hosts the target room.
launch_role play-a "$PLAY_A_HTTP" "$PLAY_A_ROUTE" "$PLAY_B_ROUTE" \
  "$PLAY_A_SPOT_ROUTER" "$PLAY_A_SPOT_PUB" "$STREAM_ENDPOINT" \
  "$LOG_DIR" key_transitions on drain_natural on
launch_role play-b "$PLAY_B_HTTP" "$PLAY_B_ROUTE" "$PLAY_A_ROUTE" \
  "$PLAY_B_SPOT_ROUTER" "$PLAY_B_SPOT_PUB" ""
wait_health "$PLAY_A_HTTP"
wait_health "$PLAY_B_HTTP"
sleep "$ROUTE_SETTLE_SECONDS"

ensure() {
  local condition_result="$1" message="$2"
  if [[ "$condition_result" != "0" ]]; then
    echo "ensure failed: $message" >&2
    exit 1
  fi
}

write_trigger_config() {
  local path="$1" scenario="$2" spot_rid="$3"
  python3 - "$path" "$STREAM_ENDPOINT" "$scenario" "$spot_rid" <<'PY'
import json
import os
import stat
import sys

path, stream_endpoint, scenario, spot_rid = sys.argv[1:]
with open(path, "w", encoding="utf-8") as file:
    json.dump({"e2e": {"streamEndpoint": stream_endpoint,
        "scenario": scenario, "spotRid": spot_rid}}, file, indent=2)
os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
PY
}

SPOT_RID="obs-room-1"
curl_local -fsS -X POST "$PLAY_B_HTTP/spot/create" -H 'Content-Type: application/json' \
  -d "{\"spotRid\":\"$SPOT_RID\"}" >"$LOG_DIR/create-room.json"
python3 - "$LOG_DIR/create-room.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["state"] in ("created", "existing"), body
PY
sleep "$ROUTE_SETTLE_SECONDS"

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "flow" ]]; then
  # OBS-A1 — one connector-generated flow id threads
  # trigger -> play-a session inbound -> play-b room-spot dispatch.
  write_trigger_config "$CONFIG_DIR/trigger-flow.json" flow "$SPOT_RID"
  "$TRIGGER" --config="$CONFIG_DIR/trigger-flow.json" >"$LOG_DIR/trigger-flow.log" 2>&1
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
  write_trigger_config "$CONFIG_DIR/trigger-error.json" error "$SPOT_RID"
  "$TRIGGER" --config="$CONFIG_DIR/trigger-error.json" >"$LOG_DIR/trigger-error.log" 2>&1
  python3 - "$LOG_DIR/play-a-flow.log" <<'PY'
import re, sys
error_flow = None
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if "phase=error" in line or "zlink dispatch error" in line:
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
  curl_local -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.metrics.evidence.json"
  curl_local -fsS "$PLAY_A_HTTP/evidence" >"$LOG_DIR/play-a.metrics.evidence.json"
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

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "fanout" ]]; then
  # OBS-A4 — one action fans out to every mesh subscriber under the same
  # flow id; a timer-originated publish starts a fresh flow (origin=timer).
  # OBS-B3 — fanout.published/received counters with the closed topic label.
  curl_local -fsS -X POST "$PLAY_A_HTTP/spot/create" -H 'Content-Type: application/json' \
    -d '{"spotRid":"obs-room-sub"}' >"$LOG_DIR/create-room-sub.json"
  sleep "$ROUTE_SETTLE_SECONDS"
  write_trigger_config "$CONFIG_DIR/trigger-fanout.json" fanout "$SPOT_RID"
  "$TRIGGER" --config="$CONFIG_DIR/trigger-fanout.json" >"$LOG_DIR/trigger-fanout.log" 2>&1
  sleep "$ROUTE_SETTLE_SECONDS"
  python3 - "$LOG_DIR/play-b-flow.log" "$LOG_DIR/play-a-flow.log" <<'PY'
import re, sys
def lines(path):
    return open(path, encoding="utf-8", errors="replace").readlines()
publish_flows = set()
for line in lines(sys.argv[1]):
    if "kind=publish" in line and "phase=sent" in line:
        m = re.search(r"flow=([0-9a-f-]{36})", line)
        if m:
            publish_flows.add(m.group(1))
assert publish_flows, "play-b has no publish sent line with flow="
subscriber_flows = set()
for line in lines(sys.argv[2]):
    if "spot_subscription" in line:
        m = re.search(r"flow=([0-9a-f-]{36})", line)
        if m:
            subscriber_flows.add(m.group(1))
shared = publish_flows & subscriber_flows
assert shared, f"subscriber lines do not share the publish flow: {publish_flows} / {subscriber_flows}"
timer_flow = any("origin=timer" in line for line in lines(sys.argv[2]))
assert timer_flow, "no origin=timer line on the timer-enabled node"
print("OBS-A4 PASS (fan-out shares one flow, timer origin starts fresh)")
PY
  curl_local -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.fanout.evidence.json"
  curl_local -fsS "$PLAY_A_HTTP/evidence" >"$LOG_DIR/play-a.fanout.evidence.json"
  python3 - "$LOG_DIR/play-b.fanout.evidence.json" "$LOG_DIR/play-a.fanout.evidence.json" <<'PY'
import json, sys
published = [m for m in json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
             if m["name"] == "zlink.fanout.published"]
received = [m for m in json.load(open(sys.argv[2], encoding="utf-8"))["metrics"]
            if m["name"] == "zlink.fanout.received"]
assert published and all(m["tags"].get("topic") == "obs.projection" for m in published), published
assert received and all(m["tags"].get("topic") == "obs.projection" for m in received), received
dropped = [m for m in json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
           if m["name"] == "zlink.fanout.dropped"]
assert not dropped, "fanout.dropped must not emit without an observable drop capability"
print("OBS-B3 PASS (fanout published/received with closed topic label)")
PY
fi

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "drain" ]]; then
  # OBS-C1 subset — draining marker keeps the peer row (draining=true),
  # readiness flips, new spot creation is rejected, drain events observed.
  curl_local -fsS -X POST "$PLAY_B_HTTP/drain" -H 'Content-Type: application/json' -d '{"deadlineMs":10000}' >/dev/null
  for _ in $(seq 1 100); do
    curl_local -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.drain.evidence.json" || true
    if python3 - "$LOG_DIR/play-b.drain.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
ok = "draining" in states and body["ready"] is False
raise SystemExit(0 if ok else 1)
PY
    then break; fi
    sleep "$EVIDENCE_POLL_SECONDS"
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
  if curl_local -fsS -X POST "$PLAY_B_HTTP/spot/create" -H 'Content-Type: application/json' -d '{"spotRid":"obs-room-rejected"}' \
      >"$LOG_DIR/create-while-draining.json" 2>/dev/null; then
    python3 - "$LOG_DIR/create-while-draining.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
raise SystemExit(0 if body.get("state") == "rejected" else 1)
PY
  fi
  echo "OBS-C1 create-rejection PASS"
  # Existing peer (play-a) stays ready and serving.
  curl_local -fsS "$PLAY_A_HTTP/evidence" >"$LOG_DIR/play-a.drain.evidence.json"
  python3 - "$LOG_DIR/play-a.drain.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["ready"] is True, "play-a lost readiness"
print("OBS-C1 peer-isolation PASS (play-a stays ready)")
PY
  # Terminal result within the deadline (no in-flight work left).
  for _ in $(seq 1 150); do
    curl_local -fsS "$PLAY_B_HTTP/evidence" >"$LOG_DIR/play-b.drain-final.evidence.json" || break
    if python3 - "$LOG_DIR/play-b.drain-final.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
raise SystemExit(0 if ("drained" in states or "force_stopping" in states) else 1)
PY
    then break; fi
    sleep "$EVIDENCE_POLL_SECONDS"
  done
  python3 - "$LOG_DIR/play-b.drain-final.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
assert "drained" in states, f"drain did not reach a terminal state: {states}"
print("OBS-C1 terminal PASS (drained)")
PY
fi

relaunch_topology() {
  local phase="$1" policy_b="${2:-}" trace_a="${3:-}" metrics_a="${4:-}"
  stop_roles
  assign_ports
  REDIS_KEY_PREFIX="zlink:cpp:observability-ops:${RUN_ID}:${phase}"
  PHASE_LOG_DIR="$LOG_DIR/$phase"
  launch_role play-a "$PLAY_A_HTTP" "$PLAY_A_ROUTE" "$PLAY_B_ROUTE" \
    "$PLAY_A_SPOT_ROUTER" "$PLAY_A_SPOT_PUB" "$STREAM_ENDPOINT" \
    "$PHASE_LOG_DIR" "${trace_a:-key_transitions}" "${metrics_a:-on}"
  launch_role play-b "$PLAY_B_HTTP" "$PLAY_B_ROUTE" "$PLAY_A_ROUTE" \
    "$PLAY_B_SPOT_ROUTER" "$PLAY_B_SPOT_PUB" "" \
    "$PHASE_LOG_DIR" key_transitions on "${policy_b:-drain_natural}"
  wait_health "$PLAY_A_HTTP"
  wait_health "$PLAY_B_HTTP"
  sleep "$ROUTE_SETTLE_SECONDS"
}

drain_and_wait_terminal() {
  local http="$1" deadline_ms="$2" evidence_out="$3"
  curl_local -fsS -X POST "$http/drain" -H 'Content-Type: application/json' \
    -d "{\"deadlineMs\":$deadline_ms}" >/dev/null
  for _ in $(seq 1 200); do
    curl_local -fsS "$http/evidence" >"$evidence_out" 2>/dev/null || break
    if python3 - "$evidence_out" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
raise SystemExit(0 if ("drained" in states or "force_stopping" in states) else 1)
PY
    then break; fi
    sleep "$EVIDENCE_POLL_SECONDS"
  done
}

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "handoff" ]]; then
  # OBS-C2 + OBS-C5(a) — drain moves the joined actor to the serving peer,
  # the transfer instruments fire, and the rolling drain ends Drained.
  relaunch_topology handoff
  curl_local -fsS -X POST "$PLAY_A_HTTP/actor/join" -H 'Content-Type: application/json' \
    -d '{"actorId":"obs-actor-1"}' >"$PHASE_LOG_DIR/join.json"
  python3 - "$PHASE_LOG_DIR/join.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["accepted"], body
PY
  curl_local -fsS -X POST "$PLAY_A_HTTP/actor/ping" -H 'Content-Type: application/json' \
    -d '{"actorId":"obs-actor-1","value":5}' >"$PHASE_LOG_DIR/ping-before.json"
  python3 - "$PHASE_LOG_DIR/ping-before.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["nodeRid"] == "play-a" and body["total"] == 5, body
PY
  # Warm the play-b -> play-a route (directory find + cross-node request)
  # before draining, so the handoff admission path has a live route mesh.
  ROUTE_WARM=1
  for _ in $(seq 1 100); do
    if curl_local -fsS -X POST "$PLAY_B_HTTP/actor/ping" -H 'Content-Type: application/json' \
        -d '{"actorId":"obs-actor-1","value":0}' >"$PHASE_LOG_DIR/ping-warm.json" 2>/dev/null; then
      ROUTE_WARM=0
      break
    fi
    sleep "$EVIDENCE_POLL_SECONDS"
  done
  ensure "$ROUTE_WARM" "handoff route warm-up (play-b -> play-a) never succeeded"
  drain_and_wait_terminal "$PLAY_A_HTTP" 20000 "$PHASE_LOG_DIR/play-a.evidence.json"
  python3 - "$PHASE_LOG_DIR/play-a.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
assert "drained" in states and "force_stopping" not in states, states
metrics = body["metrics"]
handed = [m for m in metrics if m["name"] == "zlink.drain.actors.handed_off"]
transfers = [m for m in metrics if m["name"] == "zlink.actor.transfers"]
duration = [m for m in metrics if m["name"] == "zlink.actor.transfer.duration"]
pending = [m for m in metrics if m["name"] == "zlink.actor.transfer.pending_requests.count"]
assert handed and sum(m["value"] for m in handed) >= 1, handed
assert transfers and sum(m["value"] for m in transfers) >= 1, transfers
assert duration and duration[0]["kind"] == "histogram" and duration[0]["unit"] == "s", duration
assert len(pending) == 1 and pending[0]["kind"] == "histogram", pending
print("OBS-C2 PASS (handoff completed with transfer instruments)")
print("OBS-C5a PASS (rolling drain ended Drained without force)")
PY
  curl_local -fsS -X POST "$PLAY_B_HTTP/actor/ping" -H 'Content-Type: application/json' \
    -d '{"actorId":"obs-actor-1","value":3}' >"$PHASE_LOG_DIR/ping-after.json"
  python3 - "$PHASE_LOG_DIR/ping-after.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["nodeRid"] == "play-b", body
print("OBS-C2 continuity PASS (post-move ping served by play-b)")
PY
fi

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "force" ]]; then
  # OBS-C4 + OBS-C5(b) — no eligible target (peer already draining) plus a
  # short deadline forces the drain; the held session receives
  # session-closing(server_drain) and the connector exposes closeReason.
  relaunch_topology force
  curl_local -fsS -X POST "$PLAY_A_HTTP/actor/join" -H 'Content-Type: application/json' \
    -d '{"actorId":"obs-actor-2"}' >"$PHASE_LOG_DIR/join.json"
  # the hold-session warm-up action needs a live room reachable from play-a
  # while play-a is still serving.
  curl_local -fsS -X POST "$PLAY_A_HTTP/spot/create" -H 'Content-Type: application/json' \
    -d '{"spotRid":"obs-room-force"}' >"$PHASE_LOG_DIR/create.json"
  sleep "$SCENARIO_SETTLE_SECONDS"
  drain_and_wait_terminal "$PLAY_B_HTTP" 8000 "$PHASE_LOG_DIR/play-b.evidence.json"
  write_trigger_config "$CONFIG_DIR/trigger-hold-session.json" hold-session "obs-room-force"
  "$TRIGGER" --config="$CONFIG_DIR/trigger-hold-session.json" \
    >"$PHASE_LOG_DIR/hold-session.log" 2>&1 &
  HOLD_PID=$!
  PIDS+=("$HOLD_PID")
  for _ in $(seq 1 100); do
    if grep -q "hold-session ready" "$PHASE_LOG_DIR/hold-session.log" 2>/dev/null; then break; fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  grep -q "hold-session ready" "$PHASE_LOG_DIR/hold-session.log" || {
    echo "OBS-C4 failed: hold-session never became ready" >&2
    cat "$PHASE_LOG_DIR/hold-session.log" >&2
    exit 1
  }
  drain_and_wait_terminal "$PLAY_A_HTTP" 3000 "$PHASE_LOG_DIR/play-a.evidence.json"
  python3 - "$PHASE_LOG_DIR/play-a.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
assert "force_stopping" in states, states
metrics = body["metrics"]
forced_actor = [m for m in metrics if m["name"] == "zlink.drain.forced"
                and m["tags"].get("kind") == "actor"]
forced_session = [m for m in metrics if m["name"] == "zlink.drain.forced"
                  and m["tags"].get("kind") == "session"]
assert forced_session and sum(m["value"] for m in forced_session) >= 1, forced_session
if forced_actor:
    assert sum(m["value"] for m in forced_actor) >= 1, forced_actor
    actor_outcome = "forced"
else:
    actor_outcome = "natural"
print(f"OBS-C5b PASS (actor={actor_outcome}, session=forced)")
PY
  wait "$HOLD_PID" || true
  grep -q "closeReason=server_drain" "$PHASE_LOG_DIR/hold-session.log" || {
    echo "OBS-C4 failed: connector did not observe server_drain" >&2
    cat "$PHASE_LOG_DIR/hold-session.log" >&2
    exit 1
  }
  echo "OBS-C4 PASS (forced stop notified the held session with server_drain)"
fi

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "policy" ]]; then
  # OBS-C3 — release-and-recreate frees the owner rows so the next
  # GetOrCreate rebuilds on another owner; rooms.drained counts per policy.
  relaunch_topology policy release_and_recreate
  curl_local -fsS -X POST "$PLAY_B_HTTP/spot/create" -H 'Content-Type: application/json' \
    -d "{\"spotRid\":\"$SPOT_RID\"}" >"$PHASE_LOG_DIR/create.json"
  sleep "$SCENARIO_SETTLE_SECONDS"
  drain_and_wait_terminal "$PLAY_B_HTTP" 15000 "$PHASE_LOG_DIR/play-b.evidence.json"
  python3 - "$PHASE_LOG_DIR/play-b.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
states = [event["state"] for event in body["drainEvents"]]
assert "drained" in states, states
rooms = [m for m in body["metrics"] if m["name"] == "zlink.drain.rooms.drained"]
assert rooms and all(m["tags"].get("policy") == "release_and_recreate" for m in rooms), rooms
PY
  curl_local -fsS -X POST "$PLAY_A_HTTP/spot/create" -H 'Content-Type: application/json' \
    -d "{\"spotRid\":\"$SPOT_RID\"}" >"$PHASE_LOG_DIR/recreate.json"
  python3 - "$PHASE_LOG_DIR/recreate.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["state"] in ("created", "existing"), body
print("OBS-C3 PASS (release-and-recreate freed the row; peer rebuilt the room)")
PY
fi

if [[ "$SCENARIO" == "all" || "$SCENARIO" == "offnode" ]]; then
  # OBS-A3 — a tracing-off middle node creates nothing but the flow pair
  # still crosses it; OBS-B4 — no metric reader on play-a and messaging is
  # unchanged with an empty evidence collector.
  relaunch_topology offnode "" off off
  curl_local -fsS -X POST "$PLAY_B_HTTP/spot/create" -H 'Content-Type: application/json' \
    -d "{\"spotRid\":\"$SPOT_RID\"}" >"$PHASE_LOG_DIR/create.json"
  sleep "$ROUTE_SETTLE_SECONDS"
  write_trigger_config "$CONFIG_DIR/trigger-offnode-flow.json" flow "$SPOT_RID"
  "$TRIGGER" --config="$CONFIG_DIR/trigger-offnode-flow.json" \
    >"$PHASE_LOG_DIR/trigger-flow.log" 2>&1
  python3 - "$PHASE_LOG_DIR/play-b-flow.log" "$PHASE_LOG_DIR/play-a-flow.log" <<'PY'
import os, re, sys
flows = set()
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.search(r"flow=([0-9a-f-]{36})", line)
    if m and "origin=application" in line:
        flows.add(m.group(1))
assert flows, "flow pair did not cross the tracing-off node"
if os.path.exists(sys.argv[2]):
    for line in open(sys.argv[2], encoding="utf-8", errors="replace"):
        assert "flow=" not in line, f"off node emitted a flow line: {line}"
print("OBS-A3 PASS (off node propagates without creating or logging)")
PY
  curl_local -fsS "$PLAY_A_HTTP/evidence" >"$PHASE_LOG_DIR/play-a.evidence.json"
  python3 - "$PHASE_LOG_DIR/play-a.evidence.json" <<'PY'
import json, sys
body = json.load(open(sys.argv[1], encoding="utf-8"))
assert body["metrics"] == [], "reader-less node must not accumulate metric samples"
print("OBS-B4 PASS (no reader: messaging intact, no metric accumulation)")
PY
fi

cat <<'EOF'
DEFERRED (외부 인프라 주입/스펙 표면 확정 대기, see feature-map.ko.md):
  OBS-B1 connector reconnects 계기(공개 표면 spec 확정 대기 — 서버측 connections 계기는 검증됨),
  OBS-B3 lease 지연 주입(redis 측 지연 주입 인프라 필요 — fanout 계기는 검증됨),
  OBS-C2 bound-session push 연속성 세부(단언은 post-move ping 연속성으로 대체)
EOF
echo "ObservabilityOps scenario=$SCENARIO PASS"
