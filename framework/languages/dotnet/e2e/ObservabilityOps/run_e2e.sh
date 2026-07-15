#!/usr/bin/env bash
set -euo pipefail
umask 077

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"
SCENARIO="${*:-all}"
SCENARIO="${SCENARIO// /,}"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
CONFIG_DIR="$(mktemp -d)"

PLAY_PROJECT="$ROOT_DIR/Server/Play/ObservabilityOps.Play.csproj"
SESSION_PROJECT="$ROOT_DIR/Server/Session/ObservabilityOps.Session.csproj"
WORKFLOW_PROJECT="$ROOT_DIR/Server/Workflow/ObservabilityOps.Workflow.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/ObservabilityOps.Client.csproj"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
REDIS_READINESS_TIMEOUT_SECONDS=60

read -r -a PORTS <<<"$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(16):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"

PLAY_A_URL="http://127.0.0.1:${PORTS[0]}"
PLAY_B_URL="http://127.0.0.1:${PORTS[1]}"
SESSION_URL="http://127.0.0.1:${PORTS[2]}"
WORKFLOW_A_URL="http://127.0.0.1:${PORTS[3]}"
WORKFLOW_B_URL="http://127.0.0.1:${PORTS[4]}"
PLAY_A_ROUTER="tcp://127.0.0.1:${PORTS[5]}"
PLAY_B_ROUTER="tcp://127.0.0.1:${PORTS[6]}"
SESSION_ROUTER="tcp://127.0.0.1:${PORTS[7]}"
WORKFLOW_A_ROUTER="tcp://127.0.0.1:${PORTS[8]}"
WORKFLOW_B_ROUTER="tcp://127.0.0.1:${PORTS[9]}"
PLAY_A_PUB="tcp://127.0.0.1:${PORTS[10]}"
PLAY_B_PUB="tcp://127.0.0.1:${PORTS[11]}"
SESSION_PUB="tcp://127.0.0.1:${PORTS[12]}"
WORKFLOW_A_PUB="tcp://127.0.0.1:${PORTS[13]}"
WORKFLOW_B_PUB="tcp://127.0.0.1:${PORTS[14]}"

SESSION_STREAM="tcp://127.0.0.1:${PORTS[15]}"
pids=()
REDIS_CONTAINER=""

cleanup() {
  local code=$?
  terminate_roles
  rm -rf "$CONFIG_DIR"
  [[ -n "$REDIS_CONTAINER" ]] && docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  [[ "$code" -ne 0 ]] && echo "ObservabilityOps failed. Logs: $LOG_DIR" >&2
  return "$code"
}

terminate_roles() {
  for pid in "${pids[@]:-}"; do kill -- "-$pid" >/dev/null 2>&1 || kill "$pid" >/dev/null 2>&1 || true; done
  for _ in $(seq 1 15); do
    local alive=0
    for pid in "${pids[@]:-}"; do kill -0 "$pid" >/dev/null 2>&1 && alive=1 || true; done
    [[ "$alive" -eq 0 ]] && break
    sleep 0.1
  done
  for pid in "${pids[@]:-}"; do kill -KILL -- "-$pid" >/dev/null 2>&1 || kill -KILL "$pid" >/dev/null 2>&1 || true; done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  pids=()
}
trap cleanup EXIT

wait_health() {
  local url="$1" name="$2"
  local attempts=$((LOCAL_READINESS_TIMEOUT_SECONDS * 10))
  for _ in $(seq 1 "$attempts"); do
    curl -fsS --max-time 1 "$url/health" >/dev/null 2>&1 && return 0
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $url" >&2
  return 1
}

start_role() {
  local name="$1" project="$2"; shift 2
  local config="$CONFIG_DIR/$TOPOLOGY_PHASE-$name.json"
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- "$@"
  setsid dotnet run --no-build --project "$project" -- --config "$config" \
    >"$LOG_DIR/$TOPOLOGY_PHASE-$name.stdout.log" 2>"$LOG_DIR/$TOPOLOGY_PHASE-$name.stderr.log" &
  pids+=("$!")
}

start_topology() {
  start_role play-a "$PLAY_PROJECT" --rid play-a --http-url "$PLAY_A_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$PLAY_A_ROUTER" --pub-endpoint "$PLAY_A_PUB" --log-dir "$LOG_DIR"
  wait_health "$PLAY_A_URL" play-a
  start_role play-b "$PLAY_PROJECT" --rid play-b --http-url "$PLAY_B_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$PLAY_B_ROUTER" --pub-endpoint "$PLAY_B_PUB" --log-dir "$LOG_DIR" --metrics-enabled false
  wait_health "$PLAY_B_URL" play-b
  start_role workflow-a "$WORKFLOW_PROJECT" --rid workflow-a --http-url "$WORKFLOW_A_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$WORKFLOW_A_ROUTER" --pub-endpoint "$WORKFLOW_A_PUB" --log-dir "$LOG_DIR"
  wait_health "$WORKFLOW_A_URL" workflow-a
  start_role workflow-b "$WORKFLOW_PROJECT" --rid workflow-b --http-url "$WORKFLOW_B_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$WORKFLOW_B_ROUTER" --pub-endpoint "$WORKFLOW_B_PUB" --log-dir "$LOG_DIR"
  wait_health "$WORKFLOW_B_URL" workflow-b
  start_role session-a "$SESSION_PROJECT" --rid session-a --http-url "$SESSION_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$SESSION_ROUTER" --pub-endpoint "$SESSION_PUB" \
    --stream-endpoint "$SESSION_STREAM" --preferred-play-rid play-a --log-dir "$LOG_DIR"
  wait_health "$SESSION_URL" session-a
}

run_client() {
  local scenario="$1" c5_phase="${2:-both}"
  local config="$CONFIG_DIR/client-$scenario-$c5_phase.json"
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- \
    --config-dir "$CONFIG_DIR" \
    --play-a-url "$PLAY_A_URL" --play-b-url "$PLAY_B_URL" \
    --session-url "$SESSION_URL" --session-endpoint "$SESSION_STREAM" \
    --workflow-a-url "$WORKFLOW_A_URL" --workflow-b-url "$WORKFLOW_B_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" --scenario "$scenario" --log-dir "$LOG_DIR" \
    --c5-phase "$c5_phase"
  dotnet run --no-build --project "$CLIENT_PROJECT" -- --config "$config"
}

echo "log_dir=$LOG_DIR"
dotnet build "$PLAY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$SESSION_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$WORKFLOW_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

zlink_redis_start_scoped_assign REDIS_CONTAINER REDIS_ENDPOINT \
  "zlink-redis-dotnet-e2e-observability-ops" "redis:7.2-alpine" "$LOG_DIR"
zlink_redis_wait_ready "$REDIS_CONTAINER" "$REDIS_READINESS_TIMEOUT_SECONDS"
REDIS_KEY_PREFIX="observability-ops:$RUN_ID:"
TOPOLOGY_PHASE=phase1
start_topology
if [[ "$SCENARIO" == "all" ]]; then
  run_client "OBS-A1,OBS-A2,OBS-A3,OBS-A4"
  terminate_roles
  REDIS_KEY_PREFIX="observability-ops:$RUN_ID:track-b:"
  TOPOLOGY_PHASE=track-b
  start_topology
  run_client "OBS-B1,OBS-B2,OBS-B3,OBS-B4"
  for scenario in OBS-C1 OBS-C2 OBS-C3 OBS-C4; do
    terminate_roles
    REDIS_KEY_PREFIX="observability-ops:$RUN_ID:$scenario:"
    TOPOLOGY_PHASE="${scenario,,}"
    start_topology
    run_client "$scenario"
  done
  terminate_roles
  REDIS_KEY_PREFIX="observability-ops:$RUN_ID:c5-sequential:"
  TOPOLOGY_PHASE=c5-sequential
  start_topology
  run_client OBS-C5 sequential
  terminate_roles
  REDIS_KEY_PREFIX="observability-ops:$RUN_ID:c5-simultaneous:"
  TOPOLOGY_PHASE=c5-simultaneous
  start_topology
  run_client OBS-C5 simultaneous
elif [[ "$SCENARIO" == "OBS-C5" ]]; then
  run_client OBS-C5 sequential
  terminate_roles
  REDIS_KEY_PREFIX="observability-ops:$RUN_ID:phase2:"
  TOPOLOGY_PHASE=phase2
  start_topology
  run_client OBS-C5 simultaneous
else
  run_client "$SCENARIO"
fi
