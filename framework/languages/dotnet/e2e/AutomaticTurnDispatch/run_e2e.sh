#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
DELAY_PROJECT="$SCRIPT_DIR/Server/Delay/AutomaticTurnDispatch.Delay.csproj"
PLAY_PROJECT="$SCRIPT_DIR/Server/Play/AutomaticTurnDispatch.Play.csproj"
SESSION_PROJECT="$SCRIPT_DIR/Server/Session/AutomaticTurnDispatch.Session.csproj"
CLIENT_PROJECT="$SCRIPT_DIR/Client/AutomaticTurnDispatch.Client.csproj"
DELAY_DLL="$SCRIPT_DIR/Server/Delay/bin/Debug/net8.0/AutomaticTurnDispatch.Delay.dll"
PLAY_DLL="$SCRIPT_DIR/Server/Play/bin/Debug/net8.0/AutomaticTurnDispatch.Play.dll"
SESSION_DLL="$SCRIPT_DIR/Server/Session/bin/Debug/net8.0/AutomaticTurnDispatch.Session.dll"
CLIENT_DLL="$SCRIPT_DIR/Client/bin/Debug/net8.0/AutomaticTurnDispatch.Client.dll"
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$STAMP"
if [[ "$#" -eq 0 ]]; then
  SCENARIO="all"
else
  SCENARIO="$*"
  SCENARIO="${SCENARIO// /,}"
fi
mkdir -p "$LOG_DIR"
LOCAL_READINESS_TIMEOUT_SECONDS="${ZLINK_DOTNET_E2E_READY_TIMEOUT_SECONDS:-3}"
LOCAL_READINESS_POLL_SECONDS=0.1
REDIS_READINESS_TIMEOUT_SECONDS="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
PROCESS_CLEANUP_TIMEOUT_SECONDS=35
TURN_SHUTDOWN_TIMEOUT_SECONDS=60
SCENARIO_MARKER_TIMEOUT_SECONDS=30
SHUTDOWN_CLIENT_MARKER_TIMEOUT_SECONDS=90
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
PROCESS_CLEANUP_ATTEMPTS="$(
  python3 - "$PROCESS_CLEANUP_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
TURN_SHUTDOWN_ATTEMPTS="$(
  python3 - "$TURN_SHUTDOWN_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
SCENARIO_MARKER_ATTEMPTS="$(
  python3 - "$SCENARIO_MARKER_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
SHUTDOWN_CLIENT_MARKER_ATTEMPTS="$(
  python3 - "$SHUTDOWN_CLIENT_MARKER_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"

scenario_selected() {
  local expected="$1"
  local item
  if [[ "$SCENARIO" == "all" || "$SCENARIO" == "$expected" ]]; then
    return 0
  fi
  if [[ "$expected" == "shutdown" ]]; then
    [[ "$SCENARIO" == "shutdown-wait" || "$SCENARIO" == "shutdown-recovery" ]] && return 0
  fi
  IFS=',' read -ra items <<<"$SCENARIO"
  for item in "${items[@]}"; do
    if [[ "$item" == "$expected" ]]; then
      return 0
    fi
    if [[ "$expected" == "shutdown" && ( "$item" == "shutdown-wait" || "$item" == "shutdown-recovery" ) ]]; then
      return 0
    fi
  done
  return 1
}

build_projects() {
  dotnet build "$DELAY_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$PLAY_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$SESSION_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null
}

# Portable ripgrep shim: this environment may not have rg installed.
rg() {
  local args=() pattern="" path_args=() quiet=0 invert=0
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -n) shift ;;
      -q) quiet=1; shift ;;
      -v) invert=1; pattern="$2"; shift 2 ;;
      -g) shift 2 ;;
      *) if [[ -z "$pattern" && $invert -eq 0 ]]; then pattern="$1"; else path_args+=("$1"); fi; shift ;;
    esac
  done
  local grep_args=(-rEn --include='*.cs')
  [[ $quiet -eq 1 ]] && grep_args=(-rEq --include='*.cs')
  [[ $invert -eq 1 ]] && { command grep -Ev "$pattern"; return $?; }
  if [[ ${#path_args[@]} -eq 0 ]]; then
    command grep -E "$pattern"
  else
    command grep "${grep_args[@]}" -- "$pattern" "${path_args[@]}"
  fi
}

static_checks() {
  if rg -n 'MapPost\("/await|HttpClient|new HttpClient|\.Post\(' "$SCRIPT_DIR" -g '*.cs' >/tmp/zlink-automatic-turn-dispatch-static-http.$$; then
    cat /tmp/zlink-automatic-turn-dispatch-static-http.$$ >&2
    rm -f /tmp/zlink-automatic-turn-dispatch-static-http.$$
    echo "AutomaticTurnDispatch must not start scenarios through HTTP client or /await HTTP endpoints." >&2
    return 1
  fi
  rm -f /tmp/zlink-automatic-turn-dispatch-static-http.$$

  if rg -n '\.Yield(<|\()|IZLinkYieldRequestCall|IZLinkActorYieldJoinCall' "$SCRIPT_DIR" -g '*.cs' >/tmp/zlink-automatic-turn-dispatch-static-await.$$; then
    cat /tmp/zlink-automatic-turn-dispatch-static-await.$$ >&2
    rm -f /tmp/zlink-automatic-turn-dispatch-static-await.$$
    echo "AutomaticTurnDispatch must use the single language-specific completion terminator." >&2
    return 1
  fi
  rm -f /tmp/zlink-automatic-turn-dispatch-static-await.$$

  if rg -n 'AwaitConnectorFactory|AutomaticTurnDispatchScenarioContext|WaitForPlayEvidenceAsync|ReadPlayEvidenceAsync' "$SCRIPT_DIR/Client" -g '*.cs' >/tmp/zlink-automatic-turn-dispatch-static-helper.$$; then
    cat /tmp/zlink-automatic-turn-dispatch-static-helper.$$ >&2
    rm -f /tmp/zlink-automatic-turn-dispatch-static-helper.$$
    echo "AutomaticTurnDispatch client scenarios must use the stream connector directly instead of thin helpers." >&2
    return 1
  fi
  rm -f /tmp/zlink-automatic-turn-dispatch-static-helper.$$

  if ! rg -q 'ZlinkStreamConnectorFactory\.Create' "$SCRIPT_DIR/Client/Program.cs"; then
    echo "AutomaticTurnDispatch full scenario must create and use a real stream connector directly." >&2
    return 1
  fi

  local scenario_file
  for scenario_file in "$SCRIPT_DIR"/Client/Scenarios/Atd*.cs; do
    if ! rg -q 'IZlinkStreamConnector client' "$scenario_file"; then
      echo "$scenario_file" >&2
      echo "AutomaticTurnDispatch ATD scenario files must receive the stream connector directly." >&2
      return 1
    fi
  done

  if ! rg -q 'ZlinkStreamConnectorFactory\.Create' "$SCRIPT_DIR/Client/Scenarios/ShutdownAwaitScenario.cs"; then
    echo "AutomaticTurnDispatch shutdown scenario must create and use a real stream connector directly." >&2
    return 1
  fi

  echo "ATD-E4 PASS public-await-surface=validated"
}

PIDS=()
cleanup() {
  set +e
  if [[ -n "${REDIS_CONTAINER:-}" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -INT "-$pid" 2>/dev/null || kill -INT "$pid" 2>/dev/null || true
    fi
  done
  for _ in $(seq 1 "$PROCESS_CLEANUP_ATTEMPTS"); do
    local alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        alive=1
        break
      fi
    done
    [[ "$alive" == "0" ]] && break
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "-$pid" 2>/dev/null || kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
  done
  pkill -TERM -f "$LOG_DIR" 2>/dev/null || true
  sleep "$LOCAL_READINESS_POLL_SECONDS"
  pkill -KILL -f "$LOG_DIR" 2>/dev/null || true
}
trap cleanup EXIT
trap 'cleanup; exit 143' TERM INT

allocate_ports() {
  local count="$1"
  python3 - "$count" <<'PY'
import random
import socket
import sys

count = int(sys.argv[1])
sockets = []
try:
    chosen = set()
    while len(sockets) < count:
        port = random.randint(20000, 32767)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local pid="${PIDS[-1]:-}"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
      echo "${name} exited before readiness at ${endpoint}" >&2
      return 1
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for ${name} at ${endpoint}" >&2
  return 1
}

wait_health() {
  local name="$1"
  local url="$2"
  local pid="${PIDS[-1]:-}"
  local deadline_ns
  deadline_ns="$(
    python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" <<'PY'
import sys
import time

timeout = float(sys.argv[1])
print(time.monotonic_ns() + int(timeout * 1_000_000_000))
PY
  )"
  while true; do
    local probe_timeout
    probe_timeout="$(
      python3 - "$deadline_ns" "$HTTP_PROBE_TIMEOUT_SECONDS" <<'PY'
import sys
import time

deadline_ns = int(sys.argv[1])
probe_timeout = float(sys.argv[2])
remaining = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
if remaining <= 0:
    print("0")
else:
    print(f"{min(probe_timeout, remaining):.3f}")
PY
    )"
    if [[ "$probe_timeout" == "0" ]]; then
      break
    fi
    if curl --max-time "$probe_timeout" \
      --connect-timeout "$probe_timeout" \
      -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
      echo "${name} exited before readiness at ${url}" >&2
      return 1
    fi
    python3 - "$deadline_ns" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import sys
import time

deadline_ns = int(sys.argv[1])
poll = float(sys.argv[2])
remaining = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
if remaining > 0:
    time.sleep(min(poll, remaining))
PY
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for ${name} at ${url}" >&2
  return 1
}

start_server() {
  local name="$1"
  local dll="$2"
  shift 2
  setsid dotnet "$dll" "$@" >"$LOG_DIR/${name}.stdout.log" 2>"$LOG_DIR/${name}.stderr.log" &
  PIDS+=("$!")
}

terminate_gracefully() {
  local name="$1"
  local pid="$2"
  if ! kill -0 "$pid" 2>/dev/null; then
    return 0
  fi
  kill -TERM "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 "$TURN_SHUTDOWN_ATTEMPTS"); do
    local state
    state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
    if [[ -z "$state" || "$state" == Z* ]]; then
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "${name} did not stop within ${TURN_SHUTDOWN_TIMEOUT_SECONDS}s after SIGTERM while await was pending" >&2
  return 1
}

request_shutdown() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill -TERM "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  fi
}

reap_or_kill_after_shutdown() {
  local name="$1"
  local pid="$2"
  for _ in $(seq 1 "$PROCESS_CLEANUP_ATTEMPTS"); do
    local state
    state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
    if [[ -z "$state" || "$state" == Z* ]]; then
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "${name} did not stop after the client observed shutdown; sending SIGKILL" >&2
  kill -KILL "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

wait_file_contains() {
  local file="$1"
  local pattern="$2"
  local failure="$3"
  local pid="${4:-}"
  local attempts="${5:-$SCENARIO_MARKER_ATTEMPTS}"
  for _ in $(seq 1 "$attempts"); do
    if [[ -f "$file" ]] && grep -F "$pattern" "$file" >/dev/null 2>&1; then
      return 0
    fi
    if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
      echo "$failure" >&2
      echo "client exited before marker: $pattern" >&2
      return 1
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "$failure" >&2
  echo "missing marker: $pattern" >&2
  return 1
}

wait_process_exit() {
  local name="$1"
  local pid="$2"
  for _ in $(seq 1 "$TURN_SHUTDOWN_ATTEMPTS"); do
    if [[ -r "/proc/$pid/stat" ]]; then
      local state
      state="$(awk '{print $3}' "/proc/$pid/stat")"
      if [[ "$state" == "Z" ]]; then
        wait "$pid"
        return $?
      fi
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      wait "$pid"
      return $?
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "${name} did not exit within ${TURN_SHUTDOWN_TIMEOUT_SECONDS}s after the peer shutdown closed the pending await request" >&2
  return 1
}

echo "log_dir=$LOG_DIR"
if [[ "${ZLINK_TURN_DISPATCH_SKIP_BUILD:-0}" != "1" ]]; then
  build_projects
fi
static_checks

# The run owns its Redis: a dedicated, throwaway container is the shared
# location store every server registers into (no registry process exists).
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the AutomaticTurnDispatch E2E (it provisions a dedicated Redis container)." >&2
  exit 1
fi
zlink_redis_start_scoped_assign \
  REDIS_CONTAINER \
  REDIS_ENDPOINT \
  "zlink-redis-dotnet-e2e-automatic-turn-dispatch" \
  "redis:7.2-alpine" \
  "$LOG_DIR"
zlink_redis_wait_ready "$REDIS_CONTAINER" "$REDIS_READINESS_TIMEOUT_SECONDS"
REDIS_KEY_PREFIX="awaitdispatch-e2e:$$:"

read -r DELAY_A_HTTP_PORT DELAY_A_ENDPOINT_PORT <<<"$(allocate_ports 2)"
DELAY_A_HTTP="http://127.0.0.1:${DELAY_A_HTTP_PORT}"
DELAY_A_ENDPOINT="tcp://127.0.0.1:${DELAY_A_ENDPOINT_PORT}"
start_server delay-a "$DELAY_DLL" \
  --rid delay-a \
  --http-url "$DELAY_A_HTTP" \
  --delay-endpoint "$DELAY_A_ENDPOINT" \
  --log-dir "$LOG_DIR"
wait_health delay-a "$DELAY_A_HTTP"
wait_port delay-a-channel "$DELAY_A_ENDPOINT"

read -r DELAY_B_HTTP_PORT DELAY_B_ENDPOINT_PORT <<<"$(allocate_ports 2)"
DELAY_B_HTTP="http://127.0.0.1:${DELAY_B_HTTP_PORT}"
DELAY_B_ENDPOINT="tcp://127.0.0.1:${DELAY_B_ENDPOINT_PORT}"
start_server delay-b "$DELAY_DLL" \
  --rid delay-b \
  --http-url "$DELAY_B_HTTP" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --log-dir "$LOG_DIR"
wait_health delay-b "$DELAY_B_HTTP"
wait_port delay-b-channel "$DELAY_B_ENDPOINT"

read -r PLAY_A_HTTP_PORT PLAY_A_SPOT_ROUTER_PORT PLAY_A_SPOT_PUB_PORT PLAY_A_SPOT_ROUTE_PORT PLAY_A_CONTROL_PORT <<<"$(allocate_ports 5)"
PLAY_A_HTTP="http://127.0.0.1:${PLAY_A_HTTP_PORT}"
PLAY_A_SPOT_ROUTER="tcp://127.0.0.1:${PLAY_A_SPOT_ROUTER_PORT}"
PLAY_A_SPOT_PUB="tcp://127.0.0.1:${PLAY_A_SPOT_PUB_PORT}"
PLAY_A_SPOT_ROUTE="tcp://127.0.0.1:${PLAY_A_SPOT_ROUTE_PORT}"
PLAY_A_CONTROL="tcp://127.0.0.1:${PLAY_A_CONTROL_PORT}"
start_server play-a "$PLAY_DLL" \
  --rid play-a \
  --http-url "$PLAY_A_HTTP" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --control-endpoint "$PLAY_A_CONTROL" \
  --delay-endpoint "$DELAY_A_ENDPOINT" \
  --spot-router-endpoint "$PLAY_A_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
  --spot-route-endpoint "$PLAY_A_SPOT_ROUTE" \
  --log-dir "$LOG_DIR"
PLAY_A_PID="${PIDS[-1]}"
wait_health play-a "$PLAY_A_HTTP"
wait_port play-a-control "$PLAY_A_CONTROL"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER"
wait_port play-a-spot-route "$PLAY_A_SPOT_ROUTE"

read -r PLAY_B_HTTP_PORT PLAY_B_SPOT_ROUTER_PORT PLAY_B_SPOT_PUB_PORT PLAY_B_SPOT_ROUTE_PORT PLAY_B_CONTROL_PORT <<<"$(allocate_ports 5)"
PLAY_B_HTTP="http://127.0.0.1:${PLAY_B_HTTP_PORT}"
PLAY_B_SPOT_ROUTER="tcp://127.0.0.1:${PLAY_B_SPOT_ROUTER_PORT}"
PLAY_B_SPOT_PUB="tcp://127.0.0.1:${PLAY_B_SPOT_PUB_PORT}"
PLAY_B_SPOT_ROUTE="tcp://127.0.0.1:${PLAY_B_SPOT_ROUTE_PORT}"
PLAY_B_CONTROL="tcp://127.0.0.1:${PLAY_B_CONTROL_PORT}"
start_server play-b "$PLAY_DLL" \
  --rid play-b \
  --http-url "$PLAY_B_HTTP" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --control-endpoint "$PLAY_B_CONTROL" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --spot-router-endpoint "$PLAY_B_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_B_SPOT_PUB" \
  --spot-route-endpoint "$PLAY_B_SPOT_ROUTE" \
  --log-dir "$LOG_DIR"
wait_health play-b "$PLAY_B_HTTP"
wait_port play-b-control "$PLAY_B_CONTROL"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER"
wait_port play-b-spot-route "$PLAY_B_SPOT_ROUTE"

read -r SESSION_A_HTTP_PORT SESSION_A_STREAM_PORT SESSION_A_SPOT_ROUTER_PORT SESSION_A_CONTROL_PORT <<<"$(allocate_ports 4)"
SESSION_A_HTTP="http://127.0.0.1:${SESSION_A_HTTP_PORT}"
SESSION_A_STREAM="tcp://127.0.0.1:${SESSION_A_STREAM_PORT}"
SESSION_A_SPOT_ROUTER="tcp://127.0.0.1:${SESSION_A_SPOT_ROUTER_PORT}"
SESSION_A_CONTROL="tcp://127.0.0.1:${SESSION_A_CONTROL_PORT}"
start_server session-a "$SESSION_DLL" \
  --rid session-a \
  --http-url "$SESSION_A_HTTP" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --control-endpoint "$SESSION_A_CONTROL" \
  --play-control-endpoint "$PLAY_A_CONTROL" \
  --spot-router-endpoint "$SESSION_A_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_A_STREAM" \
  --log-dir "$LOG_DIR"
wait_health session-a "$SESSION_A_HTTP"
wait_port session-a-control "$SESSION_A_CONTROL"
wait_port session-a-spot-router "$SESSION_A_SPOT_ROUTER"
wait_port session-a-stream "$SESSION_A_STREAM"

read -r SESSION_B_HTTP_PORT SESSION_B_STREAM_PORT SESSION_B_SPOT_ROUTER_PORT SESSION_B_CONTROL_PORT <<<"$(allocate_ports 4)"
SESSION_B_HTTP="http://127.0.0.1:${SESSION_B_HTTP_PORT}"
SESSION_B_STREAM="tcp://127.0.0.1:${SESSION_B_STREAM_PORT}"
SESSION_B_SPOT_ROUTER="tcp://127.0.0.1:${SESSION_B_SPOT_ROUTER_PORT}"
SESSION_B_CONTROL="tcp://127.0.0.1:${SESSION_B_CONTROL_PORT}"
start_server session-b "$SESSION_DLL" \
  --rid session-b \
  --http-url "$SESSION_B_HTTP" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --control-endpoint "$SESSION_B_CONTROL" \
  --play-control-endpoint "$PLAY_A_CONTROL" \
  --spot-router-endpoint "$SESSION_B_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_B_STREAM" \
  --log-dir "$LOG_DIR"
wait_health session-b "$SESSION_B_HTTP"
wait_port session-b-control "$SESSION_B_CONTROL"
wait_port session-b-spot-router "$SESSION_B_SPOT_ROUTER"
wait_port session-b-stream "$SESSION_B_STREAM"

sleep "$ROUTE_SETTLE_SECONDS"

if scenario_selected full; then
  dotnet "$CLIENT_DLL" \
    --scenario full \
    --session-a-stream-endpoint "$SESSION_A_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
  cat "$LOG_DIR/client.stdout.log"
  echo "ATD-D1 PASS topology=local"
  echo "ATD-E5 PASS language=dotnet scenario-ids=common"
fi

if scenario_selected shutdown; then
  SHUTDOWN_ID="ATD-E3-$(date +%s)-$$"
  SHUTDOWN_SPOT="await-shutdown-${STAMP//[^a-zA-Z0-9]/}"
  dotnet "$CLIENT_DLL" \
    --scenario shutdown-wait \
    --session-a-stream-endpoint "$SESSION_A_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --request-id "$SHUTDOWN_ID" \
    --spot-rid "$SHUTDOWN_SPOT" \
    >"$LOG_DIR/client-shutdown-wait.stdout.log" 2>"$LOG_DIR/client-shutdown-wait.stderr.log" &
  SHUTDOWN_CLIENT_PID=$!
  wait_file_contains \
    "$LOG_DIR/play-a.evidence.log" \
    "await-released|rid=play-a|spot=$SHUTDOWN_SPOT|request=$SHUTDOWN_ID" \
    "ATD-E3 pending await marker was not observed before shutdown." \
    "$SHUTDOWN_CLIENT_PID"
  request_shutdown "$PLAY_A_PID"
  wait_file_contains \
    "$LOG_DIR/client-shutdown-wait.stdout.log" \
    "automatic-turn-dispatch shutdown wait result=passed" \
    "ATD-E3 shutdown client did not observe a public shutdown error." \
    "$SHUTDOWN_CLIENT_PID" \
    "$SHUTDOWN_CLIENT_MARKER_ATTEMPTS"
  wait "$SHUTDOWN_CLIENT_PID"
  cat "$LOG_DIR/client-shutdown-wait.stdout.log"
  reap_or_kill_after_shutdown play-a "$PLAY_A_PID"

  start_server play-a "$PLAY_DLL" \
    --rid play-a \
    --http-url "$PLAY_A_HTTP" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --control-endpoint "$PLAY_A_CONTROL" \
    --delay-endpoint "$DELAY_A_ENDPOINT" \
    --spot-router-endpoint "$PLAY_A_SPOT_ROUTER" \
    --spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
    --spot-route-endpoint "$PLAY_A_SPOT_ROUTE" \
    --log-dir "$LOG_DIR"
  PLAY_A_PID="${PIDS[-1]}"
  wait_health play-a "$PLAY_A_HTTP"
  wait_port play-a-control "$PLAY_A_CONTROL"
  wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER"
  wait_port play-a-spot-route "$PLAY_A_SPOT_ROUTE"
  sleep "$ROUTE_SETTLE_SECONDS"

  dotnet "$CLIENT_DLL" \
    --scenario shutdown-recovery \
    --session-a-stream-endpoint "$SESSION_A_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --request-id "${SHUTDOWN_ID}-recovery" \
    --spot-rid "$SHUTDOWN_SPOT" \
    >"$LOG_DIR/client-shutdown-recovery.stdout.log" 2>"$LOG_DIR/client-shutdown-recovery.stderr.log"
  cat "$LOG_DIR/client-shutdown-recovery.stdout.log"
fi

echo "automatic-turn-dispatch e2e result=passed"
