#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
export ZLINK_NODE_E2E_ROOT="$NODE_ROOT/e2e"
source "$NODE_ROOT/e2e/redis-container.sh"
source "$NODE_ROOT/e2e/runner-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-full}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=150
HTTP_PROBE_TIMEOUT_SECONDS=3
CLIENT_SCENARIO="$SCENARIO"
if [[ "$CLIENT_SCENARIO" == "all" ]]; then
  CLIENT_SCENARIO="full"
fi
mkdir -p "$LOG_DIR"

needs_secondary_topology() {
  case "$CLIENT_SCENARIO" in
    full|ATD-D2|ATD-D3|ATD-D4) return 0 ;;
    *) return 1 ;;
  esac
}

used_ports=()

static_checks() {
  if rg -n "['\"]/await|fetch\\(|axios|node-fetch|undici|http\\.request|https\\.request" "$ROOT_DIR" -g '*.ts' -g '!**/dist/**' >/tmp/zlink-await-dispatch-static-http.$$; then
    cat /tmp/zlink-await-dispatch-static-http.$$ >&2
    rm -f /tmp/zlink-await-dispatch-static-http.$$
    echo "AutomaticTurnDispatch must not start scenarios through HTTP clients or /await HTTP endpoints." >&2
    return 1
  fi
  rm -f /tmp/zlink-await-dispatch-static-http.$$

  if ! rg -q 'zlinkStreamConnectorFactory\.create' "$ROOT_DIR/Client/main.ts"; then
    echo "AutomaticTurnDispatch full scenario must create and use a real stream connector directly." >&2
    return 1
  fi

  local scenario_file
  for scenario_file in "$ROOT_DIR"/Client/Scenarios/atd-*.ts; do
    if ! rg -q 'ZlinkStreamConnector' "$scenario_file"; then
      echo "$scenario_file" >&2
      echo "AutomaticTurnDispatch ATD scenario files must receive the stream connector directly." >&2
      return 1
    fi
    if rg -n 'zlinkStreamConnectorFactory|AwaitConnectorFactory|AutomaticTurnDispatchScenarioContext' "$scenario_file" >/tmp/zlink-await-dispatch-static-helper.$$; then
      cat /tmp/zlink-await-dispatch-static-helper.$$ >&2
      rm -f /tmp/zlink-await-dispatch-static-helper.$$
      echo "AutomaticTurnDispatch ATD scenario files must not hide connector usage behind scenario helpers." >&2
      return 1
    fi
    rm -f /tmp/zlink-await-dispatch-static-helper.$$
  done

  if ! rg -q 'zlinkStreamConnectorFactory\.create' "$ROOT_DIR/Client/Scenarios/shutdown-await-scenario.ts"; then
    echo "AutomaticTurnDispatch shutdown scenario must create and use a real stream connector directly." >&2
    return 1
  fi
}

wait_file_contains() {
  local file="$1"
  local pattern="$2"
  local failure="$3"
  local pid="${4:-}"
  local attempts="${5:-300}"
  for _ in $(seq 1 "$attempts"); do
    if [[ -f "$file" ]] && grep -F "$pattern" "$file" >/dev/null 2>&1; then
      return 0
    fi
    if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
      echo "$failure" >&2
      echo "client exited before marker: $pattern" >&2
      return 1
    fi
    sleep 0.1
  done
  echo "$failure" >&2
  echo "missing marker: $pattern" >&2
  return 1
}

terminate_gracefully() {
  local name="$1"
  local pid="$2"
  local shutdown_url="${3:-}"
  if ! kill -0 "$pid" 2>/dev/null; then
    return 0
  fi
  if [[ -n "$shutdown_url" ]]; then
    curl -fsS -X POST "$shutdown_url/shutdown" >/dev/null 2>&1 || true
  else
    kill -TERM "$pid" 2>/dev/null || true
  fi
  for _ in $(seq 1 600); do
    local state
    state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
    if [[ -z "$state" || "$state" == Z* ]]; then
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    sleep 0.1
  done
  kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 100); do
    local state
    state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
    if [[ -z "$state" || "$state" == Z* ]]; then
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    sleep 0.1
  done
  echo "$name did not stop while await was pending" >&2
  return 1
}

pids=()
REDIS_CONTAINER_ID=""
cleanup() {
  local code=$?
  stop_live_pids
  wait_all_pids_ignoring_status
  remove_redis_container
  if [[ "$code" -ne 0 ]]; then
    tail_failure_logs
  fi
}
trap cleanup EXIT

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/Delay"
build_package "$ROOT_DIR/Server/Play"
build_package "$ROOT_DIR/Server/Session"
build_package "$ROOT_DIR/Client"
static_checks
echo "scenario ATD-E4 passed" | tee "$LOG_DIR/static-checks.stdout.log"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run AutomaticTurnDispatch because it provisions a dedicated Redis location store." >&2
  exit 1
fi
start_redis_container "zlink-redis-node-e2e-${RANDOM}-$$" -p "127.0.0.1::6379" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
REDIS_ENDPOINT="$(redis_container_endpoint "$REDIS_CONTAINER_ID")"
REDIS_KEY_PREFIX="await-dispatch:node:${RUN_ID}:location"
wait_tcp redis "tcp://$REDIS_ENDPOINT"

DELAY_HTTP_PORT="$(allocate_port)"
DELAY_B_HTTP_PORT="$(allocate_port)"
PLAY_HTTP_PORT="$(allocate_port)"
PLAY_B_HTTP_PORT="$(allocate_port)"
SESSION_HTTP_PORT="$(allocate_port)"
SESSION_B_HTTP_PORT="$(allocate_port)"
DELAY_PORT="$(allocate_port)"
DELAY_B_PORT="$(allocate_port)"
PLAY_CONTROL_PORT="$(allocate_port)"
PLAY_B_CONTROL_PORT="$(allocate_port)"
SESSION_CONTROL_PORT="$(allocate_port)"
SESSION_B_CONTROL_PORT="$(allocate_port)"
PLAY_SPOT_ROUTE_PORT="$(allocate_port)"
PLAY_B_SPOT_ROUTE_PORT="$(allocate_port)"
SESSION_SPOT_ROUTE_PORT="$(allocate_port)"
SESSION_B_SPOT_ROUTE_PORT="$(allocate_port)"
SESSION_SPOT_ROUTER_PORT="$(allocate_port)"
SESSION_B_SPOT_ROUTER_PORT="$(allocate_port)"
PLAY_SPOT_ROUTER_PORT="$(allocate_port)"
PLAY_B_SPOT_ROUTER_PORT="$(allocate_port)"
PLAY_SPOT_PUB_PORT="$(allocate_port)"
PLAY_B_SPOT_PUB_PORT="$(allocate_port)"
SESSION_STREAM_PORT="$(allocate_port)"
SESSION_B_STREAM_PORT="$(allocate_port)"

DELAY_URL="http://127.0.0.1:$DELAY_HTTP_PORT"
DELAY_B_URL="http://127.0.0.1:$DELAY_B_HTTP_PORT"
PLAY_URL="http://127.0.0.1:$PLAY_HTTP_PORT"
PLAY_B_URL="http://127.0.0.1:$PLAY_B_HTTP_PORT"
SESSION_URL="http://127.0.0.1:$SESSION_HTTP_PORT"
SESSION_B_URL="http://127.0.0.1:$SESSION_B_HTTP_PORT"
DELAY_ENDPOINT="tcp://127.0.0.1:$DELAY_PORT"
DELAY_B_ENDPOINT="tcp://127.0.0.1:$DELAY_B_PORT"
PLAY_CONTROL="tcp://127.0.0.1:$PLAY_CONTROL_PORT"
PLAY_B_CONTROL="tcp://127.0.0.1:$PLAY_B_CONTROL_PORT"
SESSION_CONTROL="tcp://127.0.0.1:$SESSION_CONTROL_PORT"
SESSION_B_CONTROL="tcp://127.0.0.1:$SESSION_B_CONTROL_PORT"
PLAY_SPOT_ROUTE="tcp://127.0.0.1:$PLAY_SPOT_ROUTE_PORT"
PLAY_B_SPOT_ROUTE="tcp://127.0.0.1:$PLAY_B_SPOT_ROUTE_PORT"
SESSION_SPOT_ROUTE="tcp://127.0.0.1:$SESSION_SPOT_ROUTE_PORT"
SESSION_B_SPOT_ROUTE="tcp://127.0.0.1:$SESSION_B_SPOT_ROUTE_PORT"
SESSION_SPOT_ROUTER="tcp://127.0.0.1:$SESSION_SPOT_ROUTER_PORT"
SESSION_B_SPOT_ROUTER="tcp://127.0.0.1:$SESSION_B_SPOT_ROUTER_PORT"
PLAY_SPOT_ROUTER="tcp://127.0.0.1:$PLAY_SPOT_ROUTER_PORT"
PLAY_B_SPOT_ROUTER="tcp://127.0.0.1:$PLAY_B_SPOT_ROUTER_PORT"
PLAY_SPOT_PUB="tcp://127.0.0.1:$PLAY_SPOT_PUB_PORT"
PLAY_B_SPOT_PUB="tcp://127.0.0.1:$PLAY_B_SPOT_PUB_PORT"
SESSION_STREAM="tcp://127.0.0.1:$SESSION_STREAM_PORT"
SESSION_B_STREAM="tcp://127.0.0.1:$SESSION_B_STREAM_PORT"

DELAY_MAIN="$ROOT_DIR/Server/Delay/dist/Server/Delay/main.js"
PLAY_MAIN="$ROOT_DIR/Server/Play/dist/Server/Play/main.js"
SESSION_MAIN="$ROOT_DIR/Server/Session/dist/Server/Session/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_server delay-a "$DELAY_MAIN" \
  --rid delay-a \
  --http-url "$DELAY_URL" \
  --delay-endpoint "$DELAY_ENDPOINT" \
  --evidence-file "$LOG_DIR/delay-a.evidence.log"
DELAY_A_PID="${pids[-1]}"

start_server delay-b "$DELAY_MAIN" \
  --rid delay-b \
  --http-url "$DELAY_B_URL" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --evidence-file "$LOG_DIR/delay-b.evidence.log"
DELAY_B_PID="${pids[-1]}"

if needs_secondary_topology; then
  start_server play-b "$PLAY_MAIN" \
    --rid play-b \
    --http-url "$PLAY_B_URL" \
    --control-endpoint "$PLAY_B_CONTROL" \
    --spot-route-endpoint "$PLAY_B_SPOT_ROUTE" \
    --peer-spot-route-endpoint "$PLAY_SPOT_ROUTE" \
    --spot-router-endpoint "$PLAY_B_SPOT_ROUTER" \
    --spot-pub-endpoint "$PLAY_B_SPOT_PUB" \
    --delay-endpoint "$DELAY_B_ENDPOINT" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --evidence-file "$LOG_DIR/play-b.evidence.log" \
    --log-dir "$LOG_DIR"
  PLAY_B_PID="${pids[-1]}"
fi

PLAY_A_PEER_ARGS=()
if needs_secondary_topology; then
  PLAY_A_PEER_ARGS+=(--peer-spot-route-endpoint "$PLAY_B_SPOT_ROUTE")
fi

start_server play-a "$PLAY_MAIN" \
  --rid play-a \
  --http-url "$PLAY_URL" \
  --control-endpoint "$PLAY_CONTROL" \
  --spot-route-endpoint "$PLAY_SPOT_ROUTE" \
  "${PLAY_A_PEER_ARGS[@]}" \
  --spot-router-endpoint "$PLAY_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_SPOT_PUB" \
  --delay-endpoint "$DELAY_ENDPOINT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --evidence-file "$LOG_DIR/play-a.evidence.log" \
  --log-dir "$LOG_DIR"
PLAY_A_PID="${pids[-1]}"

PLAY_CONTROL_ENDPOINTS="$PLAY_CONTROL"
PLAY_SPOT_ROUTE_ENDPOINTS="$PLAY_SPOT_ROUTE"
if needs_secondary_topology; then
  PLAY_CONTROL_ENDPOINTS="$PLAY_CONTROL,$PLAY_B_CONTROL"
  PLAY_SPOT_ROUTE_ENDPOINTS="$PLAY_SPOT_ROUTE,$PLAY_B_SPOT_ROUTE"
fi

start_server session-a "$SESSION_MAIN" \
  --rid session-a \
  --http-url "$SESSION_URL" \
  --control-router-endpoint "$SESSION_CONTROL" \
  --play-control-endpoint "$PLAY_CONTROL_ENDPOINTS" \
  --spot-route-endpoint "$SESSION_SPOT_ROUTE" \
  --spot-router-endpoint "$SESSION_SPOT_ROUTER" \
  --play-spot-route-endpoint "$PLAY_SPOT_ROUTE_ENDPOINTS" \
  --stream-endpoint "$SESSION_STREAM" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --evidence-file "$LOG_DIR/session-a.evidence.log" \
  --log-dir "$LOG_DIR"
SESSION_A_PID="${pids[-1]}"
wait_health "$SESSION_URL" session-a "$SESSION_A_PID"

SESSION_B_PID=""
if needs_secondary_topology; then
  start_server session-b "$SESSION_MAIN" \
    --rid session-b \
    --http-url "$SESSION_B_URL" \
    --control-router-endpoint "$SESSION_B_CONTROL" \
    --play-control-endpoint "$PLAY_CONTROL_ENDPOINTS" \
    --spot-route-endpoint "$SESSION_B_SPOT_ROUTE" \
    --spot-router-endpoint "$SESSION_B_SPOT_ROUTER" \
    --play-spot-route-endpoint "$PLAY_SPOT_ROUTE_ENDPOINTS" \
    --stream-endpoint "$SESSION_B_STREAM" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --evidence-file "$LOG_DIR/session-b.evidence.log" \
    --log-dir "$LOG_DIR"
  SESSION_B_PID="${pids[-1]}"
fi

wait_health "$DELAY_URL" delay-a "$DELAY_A_PID"
wait_health "$DELAY_B_URL" delay-b "$DELAY_B_PID"
if [[ -n "${PLAY_B_PID:-}" ]]; then
  wait_health "$PLAY_B_URL" play-b "$PLAY_B_PID"
fi
wait_health "$PLAY_URL" play-a "$PLAY_A_PID"
wait_health "$SESSION_URL" session-a "$SESSION_A_PID"
if [[ -n "$SESSION_B_PID" ]]; then
  wait_health "$SESSION_B_URL" session-b "$SESSION_B_PID"
fi

if [[ "$CLIENT_SCENARIO" != "ATD-E3" ]]; then
  node "$CLIENT_MAIN" \
    --session-a-stream-endpoint "$SESSION_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario "$CLIENT_SCENARIO" \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

  cat "$LOG_DIR/client.stdout.log"
fi

if [[ "$CLIENT_SCENARIO" == "full" || "$CLIENT_SCENARIO" == "ATD-E3" ]]; then
  SHUTDOWN_ID="ATD-E3-$(date +%s)-$$"
  SHUTDOWN_SPOT="await-shutdown-${RUN_ID//[^a-zA-Z0-9]/}"
  node "$CLIENT_MAIN" \
    --session-a-stream-endpoint "$SESSION_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario shutdown-wait \
    --request-id "$SHUTDOWN_ID" \
    --spot-rid "$SHUTDOWN_SPOT" \
    >"$LOG_DIR/client-shutdown-wait.stdout.log" 2>"$LOG_DIR/client-shutdown-wait.stderr.log" &
  SHUTDOWN_CLIENT_PID=$!
  wait_file_contains \
    "$LOG_DIR/play-a.evidence.log" \
    "await-released|rid=play-a|spot=$SHUTDOWN_SPOT|request=$SHUTDOWN_ID" \
    "ATD-E3 pending await marker was not observed before shutdown." \
    "$SHUTDOWN_CLIENT_PID"
  terminate_gracefully play-a "$PLAY_A_PID"
  wait_file_contains \
    "$LOG_DIR/client-shutdown-wait.stdout.log" \
    "await-dispatch shutdown wait result=passed" \
    "ATD-E3 shutdown client did not observe the public closed/cancelled error." \
    "$SHUTDOWN_CLIENT_PID" \
    900
  wait "$SHUTDOWN_CLIENT_PID"
  cat "$LOG_DIR/client-shutdown-wait.stdout.log"

  terminate_gracefully session-a "$SESSION_A_PID" "$SESSION_URL"
  if [[ -n "$SESSION_B_PID" ]]; then
    terminate_gracefully session-b "$SESSION_B_PID" "$SESSION_B_URL"
  fi

  start_server play-a "$PLAY_MAIN" \
    --rid play-a \
    --http-url "$PLAY_URL" \
    --control-endpoint "$PLAY_CONTROL" \
    --spot-route-endpoint "$PLAY_SPOT_ROUTE" \
    --peer-spot-route-endpoint "$([[ "$CLIENT_SCENARIO" == "ATD-E3" ]] && echo "" || echo "$PLAY_B_SPOT_ROUTE")" \
    --spot-router-endpoint "$PLAY_SPOT_ROUTER" \
    --spot-pub-endpoint "$PLAY_SPOT_PUB" \
    --delay-endpoint "$DELAY_ENDPOINT" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --evidence-file "$LOG_DIR/play-a.evidence.log" \
    --log-dir "$LOG_DIR"
  PLAY_A_PID="${pids[-1]}"
  wait_health "$PLAY_URL" play-a "$PLAY_A_PID"

  start_server session-a "$SESSION_MAIN" \
    --rid session-a \
    --http-url "$SESSION_URL" \
    --control-router-endpoint "$SESSION_CONTROL" \
    --play-control-endpoint "$PLAY_CONTROL_ENDPOINTS" \
    --spot-route-endpoint "$SESSION_SPOT_ROUTE" \
    --spot-router-endpoint "$SESSION_SPOT_ROUTER" \
    --play-spot-route-endpoint "$PLAY_SPOT_ROUTE_ENDPOINTS" \
    --stream-endpoint "$SESSION_STREAM" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --evidence-file "$LOG_DIR/session-a.evidence.log" \
    --log-dir "$LOG_DIR"
  SESSION_A_PID="${pids[-1]}"
  wait_health "$SESSION_URL" session-a "$SESSION_A_PID"

  if [[ "$CLIENT_SCENARIO" != "ATD-E3" ]]; then
    start_server session-b "$SESSION_MAIN" \
      --rid session-b \
      --http-url "$SESSION_B_URL" \
      --control-router-endpoint "$SESSION_B_CONTROL" \
      --play-control-endpoint "$PLAY_CONTROL_ENDPOINTS" \
      --spot-route-endpoint "$SESSION_B_SPOT_ROUTE" \
      --spot-router-endpoint "$SESSION_B_SPOT_ROUTER" \
      --play-spot-route-endpoint "$PLAY_SPOT_ROUTE_ENDPOINTS" \
      --stream-endpoint "$SESSION_B_STREAM" \
      --redis-endpoint "$REDIS_ENDPOINT" \
      --redis-key-prefix "$REDIS_KEY_PREFIX" \
      --evidence-file "$LOG_DIR/session-b.evidence.log" \
      --log-dir "$LOG_DIR"
    SESSION_B_PID="${pids[-1]}"
    wait_health "$SESSION_B_URL" session-b "$SESSION_B_PID"
  fi

  node "$CLIENT_MAIN" \
    --session-a-stream-endpoint "$SESSION_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario shutdown-recovery \
    --request-id "${SHUTDOWN_ID}-recovery" \
    --spot-rid "$SHUTDOWN_SPOT" \
    >"$LOG_DIR/client-shutdown-recovery.stdout.log" 2>"$LOG_DIR/client-shutdown-recovery.stderr.log"
  cat "$LOG_DIR/client-shutdown-recovery.stdout.log"
  echo "scenario ATD-E3 passed" | tee -a "$LOG_DIR/client-shutdown-recovery.stdout.log"
fi

echo "await-dispatch e2e result=passed"
