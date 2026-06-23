#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_PROJECT="$SCRIPT_DIR/Server/SpotService.Server.csproj"
CLIENT_PROJECT="$SCRIPT_DIR/Client/SpotService.Client.csproj"
SERVER_DLL="$SCRIPT_DIR/Server/bin/Debug/net8.0/SpotService.Server.dll"
CLIENT_DLL="$SCRIPT_DIR/Client/bin/Debug/net8.0/SpotService.Client.dll"
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$STAMP"
mkdir -p "$LOG_DIR"

PIDS=()

cleanup() {
  set +e
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -INT "$pid" 2>/dev/null || true
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
      kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 24:
        port = random.randint(41000, 60999)
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
PLAY_A_HTTP="http://127.0.0.1:${PORTS[3]}"
PLAY_A_CONTROL="tcp://127.0.0.1:${PORTS[4]}"
PLAY_A_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[5]}"
PLAY_A_SPOT_PUB="tcp://127.0.0.1:${PORTS[6]}"
PLAY_A_EXTERNAL_SPOT="tcp://127.0.0.1:${PORTS[19]}"
PLAY_B_HTTP="http://127.0.0.1:${PORTS[7]}"
PLAY_B_CONTROL="tcp://127.0.0.1:${PORTS[8]}"
PLAY_B_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[9]}"
PLAY_B_SPOT_PUB="tcp://127.0.0.1:${PORTS[10]}"
SESSION_A_HTTP="http://127.0.0.1:${PORTS[11]}"
SESSION_A_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[12]}"
SESSION_A_STREAM="tcp://127.0.0.1:${PORTS[13]}"
SESSION_A_CONTROL="tcp://127.0.0.1:${PORTS[14]}"
SESSION_B_HTTP="http://127.0.0.1:${PORTS[15]}"
SESSION_B_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[16]}"
SESSION_B_STREAM="tcp://127.0.0.1:${PORTS[17]}"
SESSION_B_CONTROL="tcp://127.0.0.1:${PORTS[18]}"
CLIENT_CONTROL="tcp://127.0.0.1:${PORTS[20]}"
CLIENT_EXTERNAL_ROUTE="tcp://127.0.0.1:${PORTS[21]}"
CLIENT_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[22]}"
CLIENT_EXTERNAL_CHANNEL="tcp://127.0.0.1:${PORTS[23]}"

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
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 400); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

start_server() {
  local name="$1"
  shift
  dotnet "$SERVER_DLL" "$@" \
    >"$LOG_DIR/${name}.stdout.log" 2>"$LOG_DIR/${name}.stderr.log" &
  PIDS+=("$!")
}

echo "log_dir=$LOG_DIR"
dotnet build "$SERVER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

start_server registry \
  --role registry \
  --rid registry \
  --http-url "$REGISTRY_HTTP" \
  --registry-pub-endpoint "$REGISTRY_PUB" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --log-dir "$LOG_DIR"
wait_port registry "$REGISTRY_HTTP"
wait_port registry-router "$REGISTRY_ROUTER"

start_server play-a \
  --role play \
  --rid play-a \
  --http-url "$PLAY_A_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$PLAY_A_CONTROL" \
  --spot-router-endpoint "$PLAY_A_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
  --external-spot-endpoint "$PLAY_A_EXTERNAL_SPOT" \
  --external-client-endpoint "$CLIENT_EXTERNAL_CHANNEL" \
  --evidence-file "$LOG_DIR/play-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port play-a "$PLAY_A_HTTP"
wait_port play-a-control "$PLAY_A_CONTROL"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER"
wait_port play-a-external-spot "$PLAY_A_EXTERNAL_SPOT"

start_server play-b \
  --role play \
  --rid play-b \
  --http-url "$PLAY_B_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$PLAY_B_CONTROL" \
  --spot-router-endpoint "$PLAY_B_SPOT_ROUTER" \
  --spot-pub-endpoint "$PLAY_B_SPOT_PUB" \
  --evidence-file "$LOG_DIR/play-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port play-b "$PLAY_B_HTTP"
wait_port play-b-control "$PLAY_B_CONTROL"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER"

start_server session-a \
  --role session \
  --rid session-a \
  --http-url "$SESSION_A_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$SESSION_A_CONTROL" \
  --spot-router-endpoint "$SESSION_A_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_A_STREAM" \
  --evidence-file "$LOG_DIR/session-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port session-a "$SESSION_A_HTTP"
wait_port session-a-control "$SESSION_A_CONTROL"
wait_port session-a-spot-router "$SESSION_A_SPOT_ROUTER"
wait_port session-a-stream "$SESSION_A_STREAM"

start_server session-b \
  --role session \
  --rid session-b \
  --http-url "$SESSION_B_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$SESSION_B_CONTROL" \
  --spot-router-endpoint "$SESSION_B_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_B_STREAM" \
  --evidence-file "$LOG_DIR/session-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port session-b "$SESSION_B_HTTP"
wait_port session-b-control "$SESSION_B_CONTROL"
wait_port session-b-spot-router "$SESSION_B_SPOT_ROUTER"
wait_port session-b-stream "$SESSION_B_STREAM"

sleep 2

dotnet "$CLIENT_DLL" \
  --session-a-stream-endpoint "$SESSION_A_STREAM" \
  --session-b-stream-endpoint "$SESSION_B_STREAM" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --play-a-evidence-url "$PLAY_A_HTTP/evidence" \
  --play-b-evidence-url "$PLAY_B_HTTP/evidence" \
  --session-a-evidence-url "$SESSION_A_HTTP/evidence" \
  --play-a-rid play-a \
  --play-b-rid play-b \
  --session-a-rid session-a \
  --play-a-external-spot-endpoint "$PLAY_A_EXTERNAL_SPOT" \
  --client-control-endpoint "$CLIENT_CONTROL" \
  --client-external-route-endpoint "$CLIENT_EXTERNAL_ROUTE" \
  --client-external-channel-endpoint "$CLIENT_EXTERNAL_CHANNEL" \
  --client-spot-router-endpoint "$CLIENT_SPOT_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
