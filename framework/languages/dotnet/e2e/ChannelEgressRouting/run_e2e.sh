#!/usr/bin/env bash
set -euo pipefail
umask 077

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"

SCENARIO="${*:-all}"
SCENARIO="${SCENARIO// /,}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
if [[ "$SCENARIO" == "all" ]]; then
  cat >&2 <<'EOF'
ChannelEgressRouting 'all' is not executable yet.
Missing selectors: CH-E2E-03, CH-E2E-08,
CH-REG-02, CH-REG-05.
Run an implemented selector explicitly; the aggregate runner does not register
Config 12 until every required selector has real process evidence.
EOF
  exit 2
fi
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
CONFIG_DIR="$(mktemp -d)"
mkdir -p "$LOG_DIR"

SERVER_PROJECT="$ROOT_DIR/Server/ChannelEgressRouting.Server.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/ChannelEgressRouting.Client.csproj"
FIXTURE="$ROOT_DIR/../../../../doc/framework/common/e2e/fixtures/config-12-channel-egress-routing.json"
REDIS_CONTAINER=""
REDIS_ENDPOINT=""
pids=()
declare -A ROLE_PIDS=()

cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -- "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 0.3
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    fi
  done
  wait "${pids[@]:-}" 2>/dev/null || true
  if [[ -n "$REDIS_CONTAINER" ]]; then
    timeout -k 2s 10s docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  rm -rf "$CONFIG_DIR"
  if [[ "$code" != "0" ]]; then
    echo "ChannelEgressRouting failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

declare -A URLS=()
declare -A EVIDENCE=()

write_role_config() {
  local role="$1" rid="$2" url="$3" route_servers="$4" route_clients="$5"
  local workflow_client="$6" workflow_server="$7" weight="$8"
  local workflow_endpoint="${9:-}"
  local stream_endpoint="${10:-}"
  local config="$CONFIG_DIR/$role.json"
  local evidence="$LOG_DIR/$role.evidence.log"
  EVIDENCE["$role"]="$evidence"
  local args=(
    --role "$role"
    --rid "$rid"
    --http-url "$url"
    --redis-endpoint "$REDIS_ENDPOINT"
    --redis-key-prefix "channel-egress:$RUN_ID:"
    --evidence-file "$evidence"
    --workflow-client "$workflow_client"
    --workflow-server "$workflow_server"
    --workflow-weight "$weight"
  )
  local channel
  IFS=',' read -ra channels <<<"$route_servers"
  for channel in "${channels[@]}"; do
    [[ -z "$channel" ]] || args+=(--route-server "$channel")
  done
  IFS=',' read -ra channels <<<"$route_clients"
  for channel in "${channels[@]}"; do
    [[ -z "$channel" ]] || args+=(--route-client "$channel")
  done
  [[ -z "$workflow_endpoint" ]] || args+=(--workflow-endpoint "$workflow_endpoint")
  [[ -z "$stream_endpoint" ]] || args+=(--stream-endpoint "$stream_endpoint")
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- "${args[@]}"
}

start_role() {
  local role="$1" rid="$2" servers="$3" clients="$4"
  local workflow_client="$5" workflow_server="$6" weight="$7"
  local workflow_endpoint="${8:-}"
  local stream_endpoint="${9:-}"
  local port url
  port="$(pick_port)"
  url="http://127.0.0.1:$port"
  URLS["$role"]="$url"
  write_role_config "$role" "$rid" "$url" "$servers" "$clients" \
    "$workflow_client" "$workflow_server" "$weight" "$workflow_endpoint" \
    "$stream_endpoint"
  setsid dotnet run --no-build --project "$SERVER_PROJECT" -- \
    --config "$CONFIG_DIR/$role.json" \
    >"$LOG_DIR/$role.stdout.log" 2>"$LOG_DIR/$role.stderr.log" &
  pids+=("$!")
  ROLE_PIDS["$role"]="$!"
}

wait_json() {
  local url="$1" expression="$2" name="$3"
  local deadline=$((SECONDS + LOCAL_READINESS_TIMEOUT_SECONDS))
  while (( SECONDS < deadline )); do
    if curl --max-time 1 --connect-timeout 1 -fsS "$url" 2>/dev/null \
      | python3 -c "import json,sys; value=json.load(sys.stdin); assert ($expression)" \
        >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting for $name at $url" >&2
  curl --max-time 1 --connect-timeout 1 -fsS "$url" >&2 || true
  echo >&2
  return 1
}

python3 - "$FIXTURE" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    fixture = json.load(stream)
assert fixture["config"] == "ChannelEgressRouting"
assert fixture["roles"]["play"]["channels"]["audit.record"] == "client"
assert fixture["roles"]["workflowServer"]["clientServer"]["workflow.command"] == "client_and_server"
PY

echo "log_dir=$LOG_DIR"
zlink_redis_start_scoped_assign REDIS_CONTAINER REDIS_ENDPOINT \
  "zlink-dotnet-channel-egress" "redis:7.2-alpine" "$LOG_DIR"
zlink_redis_wait_ready "$REDIS_CONTAINER" 30 0.2

dotnet build "$SERVER_PROJECT" --maxcpucount:1
dotnet build "$CLIENT_PROJECT" --maxcpucount:1

SESSION_STREAM_ENDPOINT="tcp://127.0.0.1:$(pick_port)"
start_role session 00-session \
  "game.session" "game.play,game.api" false false 100 "" \
  "$SESSION_STREAM_ENDPOINT"
start_role play 10-play \
  "game.play" "game.session,game.api,audit.record" true false 100
start_role api 20-api \
  "game.api" "" false false 100
start_role audit 30-audit \
  "audit.record" "" false false 100
start_role workflow100 workflow-100 \
  "" "" true true 100
WORKFLOW300_ENDPOINT="tcp://127.0.0.1:$(pick_port)"
start_role workflow300 workflow-300 \
  "" "" true true 300 "$WORKFLOW300_ENDPOINT"
start_role workflow-client workflow-client \
  "" "" true false 100

for role in session play api audit workflow100 workflow300 workflow-client; do
  wait_json "${URLS[$role]}/health" \
    "'status' in value and value['status'] == 'ready'" "$role health"
done

wait_json "${URLS[session]}/topology/game" \
  "value['readyPeerCount'] >= 2 and value['channels'] and any(channel['channelName'] == 'game.session' and channel['isReady'] for channel in value['channels'])" "session game topology"
wait_json "${URLS[play]}/topology/game" \
  "value['readyPeerCount'] >= 1 and value['channels'] and any(channel['channelName'] == 'game.play' and channel['isReady'] for channel in value['channels'])" "play game topology"
wait_json "${URLS[play]}/topology/audit" \
  "value['channels'] and any(channel['channelName'] == 'audit.record' for channel in value['channels'])" "play audit topology"
wait_json "${URLS[workflow-client]}/client-server/workflow.command" \
  "value['isReady'] and value['readyTargetCount'] == 2" "workflow targets"
if [[ "$SCENARIO" == *"CH-REG-03"* || "$SCENARIO" == *"CH-E2E-09"* ]]; then
  wait_json "${URLS[play]}/fanout-status" \
    "value['isReady'] and value['readyPublisherCount'] == 1" "fanout publisher"
fi

curl --max-time 2 -fsS "${URLS[session]}/topology/game" \
  >"$LOG_DIR/session.game.topology.json"
curl --max-time 2 -fsS "${URLS[play]}/topology/game" \
  >"$LOG_DIR/play.game.topology.json"
curl --max-time 2 -fsS "${URLS[api]}/topology/game" \
  >"$LOG_DIR/api.game.topology.json"
curl --max-time 2 -fsS "${URLS[session]}/locations" \
  >"$LOG_DIR/location.topology.json"

if [[ "$SCENARIO" == *"CH-REG-05"* ]]; then
  curl --max-time 2 -fsS \
    "${URLS[workflow-client]}/client-server/workflow.command" \
    >"$LOG_DIR/workflow.before-replacement.json"
  curl --max-time 2 -fsS -X POST "${URLS[workflow300]}/shutdown" >/dev/null
  wait "${ROLE_PIDS[workflow300]}" || true
  start_role workflow300replacement workflow-300-new \
    "" "" true true 300 "$WORKFLOW300_ENDPOINT"
  wait_json "${URLS[workflow300replacement]}/health" \
    "'status' in value and value['status'] == 'ready'" \
    "workflow replacement health"
  wait_json "${URLS[workflow-client]}/client-server/workflow.command" \
    "value['isReady'] and value['readyTargetCount'] == 2" \
    "workflow replacement target"
  curl --max-time 2 -fsS \
    "${URLS[workflow-client]}/client-server/workflow.command" \
    >"$LOG_DIR/workflow.after-replacement.json"
fi

python3 - "$CONFIG_DIR/client.json" "$SCENARIO" "$SERVER_PROJECT" \
  "$CONFIG_DIR" "$REDIS_ENDPOINT" "channel-egress:$RUN_ID:" "$LOG_DIR" \
  "$SESSION_STREAM_ENDPOINT" \
  "${URLS[session]}" "${URLS[play]}" "${URLS[api]}" "${URLS[audit]}" \
  "${URLS[workflow100]}" "${URLS[workflow300]}" "${URLS[workflow-client]}" \
  "${EVIDENCE[session]}" "${EVIDENCE[play]}" "${EVIDENCE[api]}" \
  "${EVIDENCE[audit]}" "${EVIDENCE[workflow100]}" \
  "${EVIDENCE[workflow300]}" "${EVIDENCE[workflow-client]}" <<'PY'
import json
import os
import stat
import sys

(path, scenario, server_project, config_dir, redis, prefix, log_dir,
 stream_endpoint,
 session, play, api, audit, w100, w300, wc,
 es, ep, ea, eau, ew100, ew300, ewc) = sys.argv[1:]
value = {
    "Options": {
        "Scenario": scenario,
        "Urls": {
            "session": session, "play": play, "api": api, "audit": audit,
            "workflow100": w100, "workflow300": w300, "workflow-client": wc,
        },
        "EvidenceFiles": {
            "session": es, "play": ep, "api": ea, "audit": eau,
            "workflow100": ew100, "workflow300": ew300, "workflow-client": ewc,
        },
        "InvalidServerProject": server_project,
        "ConfigDir": config_dir,
        "RedisEndpoint": redis,
        "RedisKeyPrefix": prefix,
        "LogDir": log_dir,
        "StreamEndpoint": stream_endpoint,
    }
}
with open(path, "w", encoding="utf-8") as stream:
    json.dump(value, stream, indent=2)
os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
PY

dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --config "$CONFIG_DIR/client.json" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
cat "$LOG_DIR/client.stdout.log"
