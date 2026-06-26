#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REGISTRY_PROJECT="$SCRIPT_DIR/Server/Registry/YieldDispatch.Registry.csproj"
DELAY_PROJECT="$SCRIPT_DIR/Server/Delay/YieldDispatch.Delay.csproj"
PLAY_PROJECT="$SCRIPT_DIR/Server/Play/YieldDispatch.Play.csproj"
SESSION_PROJECT="$SCRIPT_DIR/Server/Session/YieldDispatch.Session.csproj"
CLIENT_PROJECT="$SCRIPT_DIR/Client/YieldDispatch.Client.csproj"
REGISTRY_DLL="$SCRIPT_DIR/Server/Registry/bin/Debug/net8.0/YieldDispatch.Registry.dll"
DELAY_DLL="$SCRIPT_DIR/Server/Delay/bin/Debug/net8.0/YieldDispatch.Delay.dll"
PLAY_DLL="$SCRIPT_DIR/Server/Play/bin/Debug/net8.0/YieldDispatch.Play.dll"
SESSION_DLL="$SCRIPT_DIR/Server/Session/bin/Debug/net8.0/YieldDispatch.Session.dll"
CLIENT_DLL="$SCRIPT_DIR/Client/bin/Debug/net8.0/YieldDispatch.Client.dll"
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$STAMP"
mkdir -p "$LOG_DIR"

build_projects() {
  dotnet build "$REGISTRY_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$DELAY_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$PLAY_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$SESSION_PROJECT" --maxcpucount:1 >/dev/null
  dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null
}

static_checks() {
  if rg -n 'MapPost\("/yield|HttpClient|new HttpClient|\.Post\(' "$SCRIPT_DIR" -g '*.cs' >/tmp/zlink-yield-dispatch-static-http.$$; then
    cat /tmp/zlink-yield-dispatch-static-http.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-http.$$
    echo "YieldDispatch must not start scenarios through HTTP client or /yield HTTP endpoints." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-http.$$

  if rg -n '\.Yield' "$SCRIPT_DIR" -g '*.cs' | rg -v 'Server/Play/PlayHostFactory.cs' >/tmp/zlink-yield-dispatch-static-yield.$$; then
    cat /tmp/zlink-yield-dispatch-static-yield.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-yield.$$
    echo "YieldDispatch may only call .Yield from Spot/Entry Spot handlers in PlayHostFactory.cs." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-yield.$$
}

PIDS=()
cleanup() {
  set +e
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -INT "-$pid" 2>/dev/null || kill -INT "$pid" 2>/dev/null || true
    fi
  done
  for _ in $(seq 1 50); do
    local alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        alive=1
        break
      fi
    done
    [[ "$alive" == "0" ]] && break
    sleep 0.1
  done
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "-$pid" 2>/dev/null || kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT
trap 'cleanup; exit 143' TERM INT

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 25:
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
)"

REGISTRY_HTTP="http://127.0.0.1:${PORTS[0]}"
REGISTRY_PUB="tcp://127.0.0.1:${PORTS[1]}"
REGISTRY_ROUTER="tcp://127.0.0.1:${PORTS[2]}"
DELAY_A_HTTP="http://127.0.0.1:${PORTS[3]}"
DELAY_A_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
DELAY_B_HTTP="http://127.0.0.1:${PORTS[5]}"
DELAY_B_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
PLAY_A_HTTP="http://127.0.0.1:${PORTS[7]}"
PLAY_A_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[8]}"
PLAY_A_SPOT_PUB="tcp://127.0.0.1:${PORTS[9]}"
PLAY_A_SPOT_ROUTE="tcp://127.0.0.1:${PORTS[10]}"
PLAY_A_CONTROL="tcp://127.0.0.1:${PORTS[11]}"
SESSION_A_HTTP="http://127.0.0.1:${PORTS[12]}"
SESSION_A_STREAM="tcp://127.0.0.1:${PORTS[13]}"
SESSION_A_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[14]}"
SESSION_A_CONTROL="tcp://127.0.0.1:${PORTS[15]}"
PLAY_B_HTTP="http://127.0.0.1:${PORTS[16]}"
PLAY_B_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[17]}"
PLAY_B_SPOT_PUB="tcp://127.0.0.1:${PORTS[18]}"
PLAY_B_SPOT_ROUTE="tcp://127.0.0.1:${PORTS[19]}"
PLAY_B_CONTROL="tcp://127.0.0.1:${PORTS[20]}"
SESSION_B_HTTP="http://127.0.0.1:${PORTS[21]}"
SESSION_B_STREAM="tcp://127.0.0.1:${PORTS[22]}"
SESSION_B_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[23]}"
SESSION_B_CONTROL="tcp://127.0.0.1:${PORTS[24]}"

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
  for _ in $(seq 1 1200); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      sleep 0.25
      return 0
    fi
    if [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null; then
      echo "${name} exited before readiness at ${endpoint}" >&2
      return 1
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

start_server() {
  local name="$1"
  local dll="$2"
  shift 2
  setsid bash -c '
    set +e
    name="$1"
    dll="$2"
    log_dir="$3"
    shift 3
    dotnet "$dll" "$@" >"$log_dir/${name}.stdout.log" 2>"$log_dir/${name}.stderr.log"
    rc=$?
    if [[ "$rc" -ge 128 ]]; then
      exit 0
    fi
    exit "$rc"
  ' bash "$name" "$dll" "$LOG_DIR" "$@" &
  PIDS+=("$!")
}

echo "log_dir=$LOG_DIR"
if [[ "${ZLINK_YIELD_DISPATCH_SKIP_BUILD:-0}" != "1" ]]; then
  build_projects
fi
static_checks

start_server registry "$REGISTRY_DLL" \
  --http-url "$REGISTRY_HTTP" \
  --registry-pub-endpoint "$REGISTRY_PUB" \
  --registry-router-endpoint "$REGISTRY_ROUTER"
wait_port registry "$REGISTRY_HTTP"
wait_port registry-router "$REGISTRY_ROUTER"

start_server delay-a "$DELAY_DLL" \
  --rid delay-a \
  --http-url "$DELAY_A_HTTP" \
  --delay-endpoint "$DELAY_A_ENDPOINT" \
  --log-dir "$LOG_DIR"
wait_port delay-a "$DELAY_A_HTTP"
wait_port delay-a-channel "$DELAY_A_ENDPOINT"

start_server delay-b "$DELAY_DLL" \
  --rid delay-b \
  --http-url "$DELAY_B_HTTP" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --log-dir "$LOG_DIR"
wait_port delay-b "$DELAY_B_HTTP"
wait_port delay-b-channel "$DELAY_B_ENDPOINT"

start_server play-a "$PLAY_DLL" \
  --rid play-a \
  --http-url "$PLAY_A_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$PLAY_A_CONTROL" \
  --delay-endpoint "$DELAY_A_ENDPOINT" \
  --spot-router-endpoint "$PLAY_A_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
  --spot-route-endpoint "$PLAY_A_SPOT_ROUTE" \
  --log-dir "$LOG_DIR"
wait_port play-a "$PLAY_A_HTTP"
wait_port play-a-control "$PLAY_A_CONTROL"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER"
wait_port play-a-spot-route "$PLAY_A_SPOT_ROUTE"

start_server play-b "$PLAY_DLL" \
  --rid play-b \
  --http-url "$PLAY_B_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$PLAY_B_CONTROL" \
  --delay-endpoint "$DELAY_B_ENDPOINT" \
  --spot-router-endpoint "$PLAY_B_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_B_SPOT_PUB" \
  --spot-route-endpoint "$PLAY_B_SPOT_ROUTE" \
  --log-dir "$LOG_DIR"
wait_port play-b "$PLAY_B_HTTP"
wait_port play-b-control "$PLAY_B_CONTROL"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER"
wait_port play-b-spot-route "$PLAY_B_SPOT_ROUTE"

start_server session-a "$SESSION_DLL" \
  --rid session-a \
  --http-url "$SESSION_A_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$SESSION_A_CONTROL" \
  --play-control-endpoint "$PLAY_A_CONTROL" \
  --spot-router-endpoint "$SESSION_A_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_A_STREAM" \
  --log-dir "$LOG_DIR"
wait_port session-a "$SESSION_A_HTTP"
wait_port session-a-control "$SESSION_A_CONTROL"
wait_port session-a-spot-router "$SESSION_A_SPOT_ROUTER"
wait_port session-a-stream "$SESSION_A_STREAM"

start_server session-b "$SESSION_DLL" \
  --rid session-b \
  --http-url "$SESSION_B_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$SESSION_B_CONTROL" \
  --play-control-endpoint "$PLAY_A_CONTROL" \
  --spot-router-endpoint "$SESSION_B_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_B_STREAM" \
  --log-dir "$LOG_DIR"
wait_port session-b "$SESSION_B_HTTP"
wait_port session-b-control "$SESSION_B_CONTROL"
wait_port session-b-spot-router "$SESSION_B_SPOT_ROUTER"
wait_port session-b-stream "$SESSION_B_STREAM"

sleep 1

dotnet "$CLIENT_DLL" \
  --session-a-stream-endpoint "$SESSION_A_STREAM" \
  --session-b-stream-endpoint "$SESSION_B_STREAM" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
cat "$LOG_DIR/client.stdout.log"
echo "yield-dispatch e2e result=passed"
