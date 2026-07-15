#!/usr/bin/env bash
# ZoneWorld (dotnet). Brings up the topology in the order §12 fixes, then runs the
# scenario client against it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../../../dotnet/samples/redis-common.sh"
SCENARIO="${1:-all}"
RUN_DIR="${SAMPLE_RUN_DIR:-$(mktemp -d)}"
RUN_ID="$(basename "$RUN_DIR")-$$-$RANDOM"
LOG_DIR="$RUN_DIR/logs"
CONFIG_DIR="$RUN_DIR/config"
mkdir -p "$LOG_DIR" "$CONFIG_DIR"

PIDS=()
REDIS_CONTAINER=""

cleanup() {
  for pid in "${PIDS[@]:-}"; do
    kill "$pid" 2>/dev/null || true
  done
  for pid in "${PIDS[@]:-}"; do
    wait "$pid" 2>/dev/null || true
  done
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run ZoneWorld (it provisions a dedicated Redis container)." >&2
  exit 1
fi
REDIS_CONTAINER="zlink-zoneworld-dotnet-redis-$RUN_ID"
zlink_redis_start_scoped_assign REDIS_CONTAINER REDIS_ENDPOINT \
  "zlink-zoneworld-dotnet-redis" redis:7.2-alpine

python3 - "$CONFIG_DIR" "$REDIS_ENDPOINT" "$RUN_ID" "$LOG_DIR" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
redis = sys.argv[2]
run_id = sys.argv[3]
log_dir = sys.argv[4]
shared = {"redisEndpoint": redis, "redisKeyPrefix": f"zoneworld-{run_id}:", "logDirectory": log_dir}

def write(name, role, value):
    (root / f"{name}.json").write_text(json.dumps({"shared": shared, role: value}), encoding="utf-8")

for index in (1, 2, 3):
    base = 48100 + index * 10
    write(f"zone-node-{index}", "zoneNode", {
        "nodeId": f"zone-node-{index}",
        "spotRouterEndpoint": f"tcp://127.0.0.1:{base}",
        "spotPubSubEndpoint": f"tcp://127.0.0.1:{base + 1}",
        "opsChannelEndpoint": f"tcp://127.0.0.1:{base + 2}",
        "actorsChannelEndpoint": f"tcp://127.0.0.1:{base + 3}",
        "bridgeEndpoint": f"tcp://127.0.0.1:{base + 4}",
        "faultTickZone": "zone-nw" if index == 1 else None,
        "disableBots": False,
    })

write("ops", "ops", {
    "streamEndpoint": "ws://127.0.0.1:48090",
    "broadcastEndpoint": "tcp://127.0.0.1:48091",
    "reportEndpoint": "tcp://127.0.0.1:48092",
})
write("gateway", "gateway", {
    "streamEndpoint": "ws://127.0.0.1:48080",
    "spotRouterEndpoint": "tcp://127.0.0.1:48081",
    "spotPubSubEndpoint": "tcp://127.0.0.1:48082",
    "nodeRid": "gw01",
})
PY

echo "==> build"
dotnet build "$ROOT_DIR/Server/Ops/ZoneWorld.Server.Ops.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Server/ZoneNode/ZoneWorld.Server.ZoneNode.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Server/Gateway/ZoneWorld.Server.Gateway.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Client/ZoneWorld.Client.csproj" -v q --nologo >/dev/null

declare -A NODE_PID

# `dotnet run` is a wrapper: killing it does not always take the server it launched with it,
# and a survivor keeps the node's routing id bound (see the orphan check above). Launch the
# built binary so the pid we record is the server itself.
BIN_DIR="bin/Debug/net8.0"
SERVER_BIN="$ROOT_DIR/Server/ZoneNode/$BIN_DIR/ZoneWorld.Server.ZoneNode"
OPS_BIN="$ROOT_DIR/Server/Ops/$BIN_DIR/ZoneWorld.Server.Ops"
GATEWAY_BIN="$ROOT_DIR/Server/Gateway/$BIN_DIR/ZoneWorld.Server.Gateway"
CLIENT_BIN="$ROOT_DIR/Client/$BIN_DIR/ZoneWorld.Client"

start() {
  local name="$1"; shift
  "$@" >>"$LOG_DIR/$name.log" 2>&1 &
  PIDS+=($!)
  NODE_PID[$name]=$!
  echo "    started $name (pid ${PIDS[-1]})"
}

# ZW-B4·ZW-C2·ZW-E5 assert what happens when a node goes away, so the runner has to take one
# away. The client cannot: it only speaks to Gateway and Ops.
stop_node() {
  local name="$1"
  local pid="${NODE_PID[$name]:-}"
  [[ -n "$pid" ]] || return 0
  echo "    stopping $name (pid $pid)"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  unset "NODE_PID[$name]"
}

start_zone_node() {
  local name="$1"
  local first_new_line first_new_ops_line
  first_new_line=$(($(wc -l <"$LOG_DIR/$name.log" 2>/dev/null || printf '0') + 1))
  first_new_ops_line=$(($(wc -l <"$LOG_DIR/ops.log" 2>/dev/null || printf '0') + 1))
  : >"$LOG_DIR/$name.restart.marker"
  start "$name" "$SERVER_BIN" --config "$CONFIG_DIR/$name.json"
  wait_for_log_after "$name" "topology=ready" "$first_new_line"
  # These two independent observations replace a fixed convergence delay: the restarted node
  # has submitted a status report, and Ops has observed the new socket connection.
  wait_for_log_after "$name" "packet=ReportNodeStatusMsg" "$first_new_line"
  wait_for_log_after ops "node connection observed. node=$name, connected=True" "$first_new_ops_line"
}

client_config() {
  local scenarios="$1" path="$CONFIG_DIR/client.json"
  python3 - "$CONFIG_DIR/ops.json" "$path" "$scenarios" <<'PY'
import json
import pathlib
import sys

source = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
source.pop("ops")
source["client"] = {
    "gatewayEndpoint": "ws://127.0.0.1:48080",
    "opsEndpoint": "ws://127.0.0.1:48090",
    "scenarios": sys.argv[3],
}
pathlib.Path(sys.argv[2]).write_text(json.dumps(source), encoding="utf-8")
PY
  printf '%s\n' "$path"
}

run_client() {
  local config
  config="$(client_config "$1")"
  "$CLIENT_BIN" --config "$config" 2>&1 | tee -a "$LOG_DIR/client.log"
  return "${PIPESTATUS[0]}"
}

# Runs a client scenario while the runner disrupts the topology underneath it.
run_client_with_stop() {
  local id="$1" node="$2" delay="$3"
  if [[ "$SCENARIO" != "all" && "$SCENARIO" != *"$id"* ]]; then return 0; fi

  run_client "$id" &
  local client_pid=$!
  sleep "$delay"
  stop_node "$node"
  wait "$client_pid" || status=1
  start_zone_node "$node"
}

wait_for_log() {
  local name="$1" pattern="$2" attempts="${3:-200}"
  for ((i = 0; i < attempts; i++)); do
    if grep -q "$pattern" "$LOG_DIR/$name.log" 2>/dev/null; then return 0; fi
    sleep 0.1
  done
  echo "!! $name never logged '$pattern'" >&2
  tail -20 "$LOG_DIR/$name.log" >&2 || true
  return 1
}

wait_for_log_after() {
  local name="$1" pattern="$2" first_line="$3" attempts="${4:-200}"
  for ((i = 0; i < attempts; i++)); do
    if tail -n +"$first_line" "$LOG_DIR/$name.log" 2>/dev/null | grep -q "$pattern"; then
      return 0
    fi
    sleep 0.1
  done
  echo "!! $name never logged '$pattern' after line $first_line" >&2
  tail -20 "$LOG_DIR/$name.log" >&2 || true
  return 1
}

echo "==> ops"
start ops "$OPS_BIN" --config "$CONFIG_DIR/ops.json"
wait_for_log ops "Application started."

echo "==> zone nodes"
# ZW-C4 needs a real spot runtime event, so zone-node-1's zone-nw tick is made to throw
# once. `env` keeps the switch on this one process — the other nodes must stay healthy.
start zone-node-1 "$SERVER_BIN" --config "$CONFIG_DIR/zone-node-1.json"
start zone-node-2 "$SERVER_BIN" --config "$CONFIG_DIR/zone-node-2.json"
wait_for_log zone-node-1 "topology=ready"
wait_for_log zone-node-2 "topology=ready"

# The third node hosts no zone: it only subscribes to the broadcast channel. It exists to
# show that Ops publishes without a node list — adding a node changes nothing on the
# publishing side (ZW-D2, scenario §11.1).
echo "==> zone-node-3 (fanout subscriber only)"
start zone-node-3 "$SERVER_BIN" --config "$CONFIG_DIR/zone-node-3.json"
wait_for_log zone-node-3 "topology=ready"

echo "==> gateway"
start gateway "$GATEWAY_BIN" --config "$CONFIG_DIR/gateway.json"
wait_for_log gateway "Application started."

echo "==> scenarios ($SCENARIO)"
set +e
CLIENT_SCENARIO="$(printf '%s' "$SCENARIO" | tr ',' '\n' | grep -vxE 'ZW-D2|ZW-F2|ZW-C2|ZW-C3|ZW-B4|ZW-E5|ZW-E5-arm' | paste -sd, -)"
if [[ "$SCENARIO" == "all" || -n "$CLIENT_SCENARIO" ]]; then
  CLIENT_CONFIG="$(client_config "${CLIENT_SCENARIO:-$SCENARIO}")"
  "$CLIENT_BIN" --config "$CLIENT_CONFIG" 2>&1 | tee "$LOG_DIR/client.log"
  status=${PIPESTATUS[0]}
else
  status=0
fi
set -e

# Some of what §11 asks for is not observable from a client: the absence of a client (ZW-F2), a
# node the client is never told about (ZW-D2), another node's fanout subscriber (ZW-D1), the
# whole bot population (ZW-F1), or a push that must never be attempted (ZW-F3). The runner reads
# those out of the server logs.
# The client writes its verdicts to client.log; these write theirs here. The §12 markers below
# are decided from both, so a runner verdict has to be recorded, not just printed.
RUNNER_LOG="$LOG_DIR/runner.log"
: >"$RUNNER_LOG"

pass() { echo "scenario $1 passed" | tee -a "$RUNNER_LOG"; }
fail() { echo "scenario $1 FAILED: $2" >&2; echo "scenario $1 failed" >>"$RUNNER_LOG"; status=1; }

# A runner verdict such as ZW-D1-spots belongs to ZW-D1: it asserts the half of that scenario a
# client cannot see. Selecting ZW-D1 has to select it too, or a targeted run quietly proves less
# than the same scenario proves in a full one.
selects() {
  [[ "$SCENARIO" == "all" ]] && return 0
  local base
  base="$(printf '%s' "$1" | cut -d- -f1,2)"
  [[ "$SCENARIO" == *"$base"* ]]
}

runner_scenario() {
  local id="$1" description="$2" log="$3" pattern="$4"
  selects "$id" || return 0
  if grep -q "$pattern" "$LOG_DIR/$log" 2>/dev/null; then
    pass "$id"
  else
    fail "$id" "$description"
  fi
}

# Passes only when the pattern appears in *every* named log.
runner_scenario_all() {
  local id="$1" description="$2" pattern="$3"; shift 3
  selects "$id" || return 0
  local log
  for log in "$@"; do
    if ! grep -q "$pattern" "$LOG_DIR/$log" 2>/dev/null; then
      fail "$id" "$description ($log)"
      return 0
    fi
  done
  pass "$id"
}

# Passes only when the pattern appears nowhere. An assertion about something that must not
# happen has to be written as an absence, or it asserts nothing.
runner_scenario_absent() {
  local id="$1" description="$2" pattern="$3"; shift 3
  selects "$id" || return 0
  local hits
  hits="$(grep -l "$pattern" "${@/#/$LOG_DIR/}" 2>/dev/null || true)"
  if [[ -n "$hits" ]]; then
    fail "$id" "$description ($hits)"
  else
    pass "$id"
  fi
}

# These all need zone-node-2 to disappear while a client is watching, and each one restarts it
# afterwards. ZW-B4 goes first because it is the only one that has to *use* zone-node-2 before
# taking it away — it walks a player into zone-ne — and a node that has just come back is the
# least reliable thing to walk into.
run_client_with_stop ZW-B4 zone-node-2 20
run_client_with_stop ZW-C2 zone-node-2 6
run_client_with_stop ZW-C3 zone-node-2 6

# ZW-E5: the operator closes a node, the node restarts, and it comes back still closed —
# the desired state lives in the store, not in a message.
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ZW-E5"* ]]; then
  run_client ZW-E5-arm || status=1
  stop_node zone-node-2
  start_zone_node zone-node-2
  run_client ZW-E5 || status=1
fi

# ZW-D1: one publish, no node list, and it comes out of *both* nodes' fanout subscribers and
# reaches *every* zone spot. The client half of ZW-D1 asserts what a client can see — that an
# announcement it receives is never a duplicate — so these carry their own ids: a client verdict
# and a runner verdict on the same id would be indistinguishable in the log.
runner_scenario_all ZW-D1-subscribers "a node's fanout subscriber never received the announcement" \
  "fanout subscriber received announcement" zone-node-1.log zone-node-2.log
runner_scenario_all ZW-D1-spots "a zone spot never received the announcement" \
  "zone spot: announcement delivered" zone-node-1.log zone-node-2.log

# ZW-F1: the world holds eight bots. No client sees them all — a client only ever receives one
# zone's view — so the population is counted here.
if selects ZW-F1-population; then
  bots="$(grep -ho "bot spawned. bot=[a-z0-9-]*" "$LOG_DIR"/zone-node-*.log 2>/dev/null \
    | sort -u | wc -l)"
  if [[ "$bots" -eq 8 ]]; then
    pass ZW-F1-population
  else
    fail ZW-F1-population "the world holds $bots bots, not 8"
  fi
fi

# ZW-F3: nothing is ever pushed to an actor with no bound session. That is an absence, so it is
# asserted as one: a push to an unbound actor leaves this error behind, and it must not be there.
runner_scenario_absent ZW-F3-no-push "a push was attempted to an actor with no bound session" \
  "No current session binding exists for actor" \
  zone-node-1.log zone-node-2.log zone-node-3.log

# ZW-D2: the third node received the announcement with no change to Ops.
runner_scenario ZW-D2 "zone-node-3 never received a world announcement" \
  zone-node-3.log "fanout subscriber received announcement"

# ZW-F2: a bot crossed to the other node. The bots run with no client attached, so a
# transfer recorded here proves the actor moved without a bound session.
runner_scenario ZW-F2 "no bot transferred across nodes" \
  zone-node-2.log "player entered. zone=zone-ne, player=bot-nw-x, bot=True, from=zone-node-1"

# §12 asks the sample to say what it proved, not just that it exited zero. The runner owns these
# markers because no single client run sees the whole suite: the client's batch leaves out the
# scenarios that need a node taken away, and six more are judged from server logs. A marker is
# printed only when every scenario behind it actually passed — a phase that says "completed"
# without having run is worse than no marker at all.
declare -A PASSED=()
while read -r id; do PASSED["$id"]=1; done < <(
  sed -nE 's/^scenario ([A-Za-z0-9-]+) passed$/\1/p' \
    "$LOG_DIR/client.log" "$RUNNER_LOG" 2>/dev/null
)

phase() {
  local marker="$1"; shift
  local id
  for id in "$@"; do
    if [[ -z "${PASSED[$id]:-}" ]]; then
      echo "!! $marker withheld: $id did not pass" >&2
      return 0
    fi
  done
  echo "$marker"
}

# Only a full run can claim a phase. A selective run proves one scenario, not a capability.
if [[ "$SCENARIO" == "all" ]]; then
  phase "zoneworld-transfer=completed"        ZW-B2 ZW-B3 ZW-F2
  phase "zoneworld-border-sync=completed"     ZW-B1 ZW-B4
  phase "zoneworld-ops-observe=completed"     ZW-C1 ZW-C2 ZW-C3 ZW-C4
  phase "zoneworld-ops-announce=completed"    ZW-D1 ZW-D1-subscribers ZW-D1-spots ZW-D2
  phase "zoneworld-ops-maintenance=completed" ZW-E1 ZW-E2 ZW-E3 ZW-E4 ZW-E5 ZW-E6

  # The whole of §11, plus the six verdicts only the runner can reach. Gated on status as well:
  # a scenario can fail without its id ever reaching the log.
  phase "zoneworld=completed" \
    ZW-A1 ZW-A2 ZW-A3 ZW-A4 ZW-A5 \
    ZW-B1 ZW-B2 ZW-B3 ZW-B4 \
    ZW-C1 ZW-C2 ZW-C3 ZW-C4 \
    ZW-D1 ZW-D1-subscribers ZW-D1-spots ZW-D2 \
    ZW-E1 ZW-E2 ZW-E3 ZW-E4 ZW-E5 ZW-E5-arm ZW-E6 \
    ZW-F1 ZW-F1-population ZW-F2 ZW-F3 ZW-F3-no-push ZW-F4
fi

echo "==> logs: $LOG_DIR"
exit "$status"
