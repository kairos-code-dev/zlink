#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-full}"
CLIENT_SCENARIO="$SCENARIO"
if [[ "$CLIENT_SCENARIO" == "all" ]]; then
  CLIENT_SCENARIO="full"
fi
mkdir -p "$LOG_DIR"

pick_port() {
  node - <<'NODE'
const net = require('node:net');
const blocked = new Set([
  1, 7, 9, 11, 13, 15, 17, 19, 20, 21, 22, 23, 25, 37, 42, 43, 53, 69, 77, 79, 87, 95, 101, 102, 103, 104,
  109, 110, 111, 113, 115, 117, 119, 123, 135, 137, 139, 143, 161, 179, 389, 427, 465, 512, 513, 514, 515,
  526, 530, 531, 532, 540, 548, 554, 556, 563, 587, 601, 636, 989, 990, 993, 995, 1719, 1720, 1723, 2049,
  3659, 4045, 4190, 5060, 5061, 6000, 6566, 6665, 6666, 6667, 6668, 6669, 6697, 10080
]);

function tryPort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1', () => {
    const port = server.address().port;
    server.close(() => {
      if (blocked.has(port)) {
        tryPort();
      } else {
        console.log(port);
      }
    });
  });
}

tryPort();
NODE
}

build_package() {
  local dir="$1"
  (cd "$dir" && npm run build >/dev/null)
}

static_checks() {
  if rg -n "['\"]/yield|fetch\\(|axios|node-fetch|undici|http\\.request|https\\.request" "$ROOT_DIR" -g '*.ts' -g '!**/dist/**' >/tmp/zlink-yield-dispatch-static-http.$$; then
    cat /tmp/zlink-yield-dispatch-static-http.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-http.$$
    echo "YieldDispatch must not start scenarios through HTTP clients or /yield HTTP endpoints." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-http.$$

  if rg -n '\.yield(<|\()' "$ROOT_DIR" -g '*.ts' -g '!**/dist/**' | rg -v '/Server/Play/' >/tmp/zlink-yield-dispatch-static-yield.$$; then
    cat /tmp/zlink-yield-dispatch-static-yield.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-yield.$$
    echo "YieldDispatch may only call .yield from Spot, actor, timer, or worker handlers in Server/Play." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-yield.$$

  if ! rg -q 'zlinkStreamConnectorFactory\.create' "$ROOT_DIR/Client/main.ts"; then
    echo "YieldDispatch full scenario must create and use a real stream connector directly." >&2
    return 1
  fi

  local scenario_file
  for scenario_file in "$ROOT_DIR"/Client/Scenarios/yd-*.ts; do
    if ! rg -q 'ZlinkStreamConnector' "$scenario_file"; then
      echo "$scenario_file" >&2
      echo "YieldDispatch YD scenario files must receive the stream connector directly." >&2
      return 1
    fi
    if rg -n 'zlinkStreamConnectorFactory|YieldConnectorFactory|YieldDispatchScenarioContext' "$scenario_file" >/tmp/zlink-yield-dispatch-static-helper.$$; then
      cat /tmp/zlink-yield-dispatch-static-helper.$$ >&2
      rm -f /tmp/zlink-yield-dispatch-static-helper.$$
      echo "YieldDispatch YD scenario files must not hide connector usage behind scenario helpers." >&2
      return 1
    fi
    rm -f /tmp/zlink-yield-dispatch-static-helper.$$
  done

  if ! rg -q 'zlinkStreamConnectorFactory\.create' "$ROOT_DIR/Client/Scenarios/shutdown-yield-scenario.ts"; then
    echo "YieldDispatch shutdown scenario must create and use a real stream connector directly." >&2
    return 1
  fi
}

wait_health() {
  local url="$1"
  local name="$2"
  local pid="${3:-}"
  for _ in $(seq 1 120); do
    if curl -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
      echo "$name exited before readiness at $url" >&2
      return 1
    fi
    sleep 0.25
  done
  echo "Timed out waiting for $name at $url" >&2
  return 1
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
  if ! kill -0 "$pid" 2>/dev/null; then
    return 0
  fi
  kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 600); do
    local state
    state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
    if [[ -z "$state" || "$state" == Z* ]]; then
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    sleep 0.1
  done
  echo "$name did not stop after SIGTERM while yield was pending" >&2
  return 1
}

pids=()
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  wait "${pids[@]:-}" 2>/dev/null || true
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. log_dir=$LOG_DIR" >&2
    for file in "$LOG_DIR"/*.stderr.log "$LOG_DIR"/client.stderr.log; do
      if [[ -f "$file" ]]; then
        echo "----- $file -----" >&2
        tail -n 80 "$file" >&2 || true
      fi
    done
  fi
}
trap cleanup EXIT

start_server() {
  local name="$1"
  local main="$2"
  shift
  shift
  node "$main" "$@" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/Registry"
build_package "$ROOT_DIR/Server/Delay"
build_package "$ROOT_DIR/Server/Play"
build_package "$ROOT_DIR/Server/Session"
build_package "$ROOT_DIR/Client"
static_checks
echo "scenario YD-E4 passed" | tee "$LOG_DIR/static-checks.stdout.log"

REG_HTTP_PORT="$(pick_port)"
DELAY_HTTP_PORT="$(pick_port)"
DELAY_B_HTTP_PORT="$(pick_port)"
PLAY_HTTP_PORT="$(pick_port)"
PLAY_B_HTTP_PORT="$(pick_port)"
SESSION_HTTP_PORT="$(pick_port)"
SESSION_B_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
DELAY_PORT="$(pick_port)"
DELAY_B_PORT="$(pick_port)"
PLAY_CONTROL_PORT="$(pick_port)"
PLAY_B_CONTROL_PORT="$(pick_port)"
SESSION_CONTROL_PORT="$(pick_port)"
SESSION_B_CONTROL_PORT="$(pick_port)"
PLAY_SPOT_ROUTE_PORT="$(pick_port)"
PLAY_B_SPOT_ROUTE_PORT="$(pick_port)"
SESSION_SPOT_ROUTE_PORT="$(pick_port)"
SESSION_B_SPOT_ROUTE_PORT="$(pick_port)"
SESSION_SPOT_ROUTER_PORT="$(pick_port)"
SESSION_B_SPOT_ROUTER_PORT="$(pick_port)"
PLAY_SPOT_ROUTER_PORT="$(pick_port)"
PLAY_B_SPOT_ROUTER_PORT="$(pick_port)"
PLAY_SPOT_PUB_PORT="$(pick_port)"
PLAY_B_SPOT_PUB_PORT="$(pick_port)"
SESSION_STREAM_PORT="$(pick_port)"
SESSION_B_STREAM_PORT="$(pick_port)"

REG_URL="http://127.0.0.1:$REG_HTTP_PORT"
DELAY_URL="http://127.0.0.1:$DELAY_HTTP_PORT"
DELAY_B_URL="http://127.0.0.1:$DELAY_B_HTTP_PORT"
PLAY_URL="http://127.0.0.1:$PLAY_HTTP_PORT"
PLAY_B_URL="http://127.0.0.1:$PLAY_B_HTTP_PORT"
SESSION_URL="http://127.0.0.1:$SESSION_HTTP_PORT"
SESSION_B_URL="http://127.0.0.1:$SESSION_B_HTTP_PORT"
REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
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

REGISTRY_MAIN="$ROOT_DIR/Server/Registry/dist/Server/Registry/main.js"
DELAY_MAIN="$ROOT_DIR/Server/Delay/dist/Server/Delay/main.js"
PLAY_MAIN="$ROOT_DIR/Server/Play/dist/Server/Play/main.js"
SESSION_MAIN="$ROOT_DIR/Server/Session/dist/Server/Session/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_server registry "$REGISTRY_MAIN" \
  --rid registry \
  --http-url "$REG_URL" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER"
wait_health "$REG_URL" registry "${pids[-1]}"

start_server delay-a "$DELAY_MAIN" \
  --rid delay-a \
  --http-url "$DELAY_URL" \
  --delay-endpoint "$DELAY_ENDPOINT" \
  --evidence-file "$LOG_DIR/delay-a.evidence.log"
wait_health "$DELAY_URL" delay-a "${pids[-1]}"

start_server delay-b "$DELAY_MAIN" \
  --rid delay-b \
  --http-url "$DELAY_B_URL" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --evidence-file "$LOG_DIR/delay-b.evidence.log"
wait_health "$DELAY_B_URL" delay-b "${pids[-1]}"

start_server play-a "$PLAY_MAIN" \
  --rid play-a \
  --http-url "$PLAY_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --control-endpoint "$PLAY_CONTROL" \
  --spot-route-endpoint "$PLAY_SPOT_ROUTE" \
  --spot-router-endpoint "$PLAY_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_SPOT_PUB" \
  --delay-endpoint "$DELAY_ENDPOINT" \
  --evidence-file "$LOG_DIR/play-a.evidence.log" \
  --log-dir "$LOG_DIR"
PLAY_A_PID="${pids[-1]}"
wait_health "$PLAY_URL" play-a "$PLAY_A_PID"

start_server play-b "$PLAY_MAIN" \
  --rid play-b \
  --http-url "$PLAY_B_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --control-endpoint "$PLAY_B_CONTROL" \
  --spot-route-endpoint "$PLAY_B_SPOT_ROUTE" \
  --spot-router-endpoint "$PLAY_B_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_B_SPOT_PUB" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --evidence-file "$LOG_DIR/play-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$PLAY_B_URL" play-b "${pids[-1]}"

start_server session-a "$SESSION_MAIN" \
  --rid session-a \
  --http-url "$SESSION_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --control-router-endpoint "$SESSION_CONTROL" \
  --play-control-endpoint "$PLAY_CONTROL,$PLAY_B_CONTROL" \
  --spot-route-endpoint "$SESSION_SPOT_ROUTE" \
  --spot-router-endpoint "$SESSION_SPOT_ROUTER" \
  --play-spot-route-endpoint "$PLAY_SPOT_ROUTE,$PLAY_B_SPOT_ROUTE" \
  --stream-endpoint "$SESSION_STREAM" \
  --evidence-file "$LOG_DIR/session-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$SESSION_URL" session-a "${pids[-1]}"

start_server session-b "$SESSION_MAIN" \
  --rid session-b \
  --http-url "$SESSION_B_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --control-router-endpoint "$SESSION_B_CONTROL" \
  --play-control-endpoint "$PLAY_CONTROL,$PLAY_B_CONTROL" \
  --spot-route-endpoint "$SESSION_B_SPOT_ROUTE" \
  --spot-router-endpoint "$SESSION_B_SPOT_ROUTER" \
  --play-spot-route-endpoint "$PLAY_SPOT_ROUTE,$PLAY_B_SPOT_ROUTE" \
  --stream-endpoint "$SESSION_B_STREAM" \
  --evidence-file "$LOG_DIR/session-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$SESSION_B_URL" session-b "${pids[-1]}"

if [[ "$CLIENT_SCENARIO" != "YD-E3" ]]; then
  node "$CLIENT_MAIN" \
    --session-a-stream-endpoint "$SESSION_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario "$CLIENT_SCENARIO" \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

  cat "$LOG_DIR/client.stdout.log"
fi

if [[ "$CLIENT_SCENARIO" == "full" || "$CLIENT_SCENARIO" == "YD-E3" ]]; then
  SHUTDOWN_ID="YD-E3-$(date +%s)-$$"
  SHUTDOWN_SPOT="yield-shutdown-${RUN_ID//[^a-zA-Z0-9]/}"
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
    "yield-released|rid=play-a|spot=$SHUTDOWN_SPOT|request=$SHUTDOWN_ID" \
    "YD-E3 pending yield marker was not observed before shutdown." \
    "$SHUTDOWN_CLIENT_PID"
  terminate_gracefully play-a "$PLAY_A_PID"
  wait_file_contains \
    "$LOG_DIR/client-shutdown-wait.stdout.log" \
    "yield-dispatch shutdown wait result=passed" \
    "YD-E3 shutdown client did not observe the public closed/cancelled error." \
    "$SHUTDOWN_CLIENT_PID" \
    900
  wait "$SHUTDOWN_CLIENT_PID"
  cat "$LOG_DIR/client-shutdown-wait.stdout.log"

  start_server play-a "$PLAY_MAIN" \
    --rid play-a \
    --http-url "$PLAY_URL" \
    --registry-router-endpoint "$REG_ROUTER" \
    --control-endpoint "$PLAY_CONTROL" \
    --spot-route-endpoint "$PLAY_SPOT_ROUTE" \
    --spot-router-endpoint "$PLAY_SPOT_ROUTER" \
    --spot-pub-endpoint "$PLAY_SPOT_PUB" \
    --delay-endpoint "$DELAY_ENDPOINT" \
    --evidence-file "$LOG_DIR/play-a.evidence.log" \
    --log-dir "$LOG_DIR"
  PLAY_A_PID="${pids[-1]}"
  wait_health "$PLAY_URL" play-a "$PLAY_A_PID"

  node "$CLIENT_MAIN" \
    --session-a-stream-endpoint "$SESSION_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario shutdown-recovery \
    --request-id "${SHUTDOWN_ID}-recovery" \
    --spot-rid "$SHUTDOWN_SPOT" \
    >"$LOG_DIR/client-shutdown-recovery.stdout.log" 2>"$LOG_DIR/client-shutdown-recovery.stderr.log"
  cat "$LOG_DIR/client-shutdown-recovery.stdout.log"
  echo "scenario YD-E3 passed" | tee -a "$LOG_DIR/client-shutdown-recovery.stdout.log"
  echo "yield-dispatch e2e result=passed" | tee -a "$LOG_DIR/client-shutdown-recovery.stdout.log"
fi
