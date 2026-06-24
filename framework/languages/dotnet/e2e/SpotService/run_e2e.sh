#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_PROJECT="$SCRIPT_DIR/Server/SpotService.Server.csproj"
CLIENT_PROJECT="$SCRIPT_DIR/Client/SpotService.Client.csproj"
SERVER_DLL="$SCRIPT_DIR/Server/bin/Debug/net8.0/SpotService.Server.dll"
CLIENT_DLL="$SCRIPT_DIR/Client/bin/Debug/net8.0/SpotService.Client.dll"
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$STAMP"
SCENARIO_SET="${SCENARIO_SET:-all}"
mkdir -p "$LOG_DIR"

if [[ "$SCENARIO_SET" == "all" && "${ZLINK_SPOT_SERVICE_ALL_CHILD:-0}" != "1" ]]; then
  echo "log_dir=$LOG_DIR"
  for child_set in baseline-1 track-c sm-e1-f4 sm-e2-e3 sm-a7-a8-c4 sm-e4 baseline-2b sm-g2 sm-g3 sm-g4 sm-g1; do
    child_ok=0
    for attempt in 1 2; do
      echo "child scenario_set=${child_set} attempt=${attempt}"
      if SCENARIO_SET="$child_set" ZLINK_SPOT_SERVICE_ALL_CHILD=1 "$0"; then
        child_ok=1
        break
      fi
      sleep 1
    done
    if [[ "$child_ok" != "1" ]]; then
      echo "child scenario_set=${child_set} failed after retries" >&2
      exit 1
    fi
  done
  echo "spot-service e2e result=passed"
  exit 0
fi

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
    while len(sockets) < 28:
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
PLAY_B_EXTERNAL_SPOT="tcp://127.0.0.1:${PORTS[26]}"
SESSION_A_HTTP="http://127.0.0.1:${PORTS[11]}"
SESSION_A_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[12]}"
SESSION_A_STREAM="tcp://127.0.0.1:${PORTS[13]}"
SESSION_A_TLS_STREAM="tls://127.0.0.1:${PORTS[25]}"
SESSION_A_CONTROL="tcp://127.0.0.1:${PORTS[14]}"
SESSION_B_HTTP="http://127.0.0.1:${PORTS[15]}"
SESSION_B_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[16]}"
SESSION_B_STREAM="tcp://127.0.0.1:${PORTS[17]}"
SESSION_B_CONTROL="tcp://127.0.0.1:${PORTS[18]}"
CLIENT_CONTROL="tcp://127.0.0.1:${PORTS[20]}"
CLIENT_EXTERNAL_ROUTE="tcp://127.0.0.1:${PORTS[21]}"
CLIENT_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[22]}"
CLIENT_EXTERNAL_CHANNEL="tcp://127.0.0.1:${PORTS[23]}"
CLIENT_SPOT_PUB="tcp://127.0.0.1:${PORTS[24]}"
CLIENT_EXTERNAL_ROUTE_B="tcp://127.0.0.1:${PORTS[27]}"

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#tls://}"
  echo "${endpoint##*:}"
}

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#tls://}"
  echo "${endpoint%:*}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 1000); do
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
TLS_CERT="$LOG_DIR/session-a-tls.crt"
TLS_KEY="$LOG_DIR/session-a-tls.key"
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$TLS_KEY" \
  -out "$TLS_CERT" \
  -days 1 \
  -subj "/CN=localhost" >/dev/null 2>&1

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
  --client-spot-pub-endpoint "$CLIENT_SPOT_PUB" \
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
  --external-spot-endpoint "$PLAY_B_EXTERNAL_SPOT" \
  --evidence-file "$LOG_DIR/play-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port play-b "$PLAY_B_HTTP"
wait_port play-b-control "$PLAY_B_CONTROL"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER"
wait_port play-b-external-spot "$PLAY_B_EXTERNAL_SPOT"

start_server session-a \
  --role session \
  --rid session-a \
  --http-url "$SESSION_A_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --control-endpoint "$SESSION_A_CONTROL" \
  --spot-router-endpoint "$SESSION_A_SPOT_ROUTER" \
  --stream-endpoint "$SESSION_A_STREAM" \
  --tls-stream-endpoint "$SESSION_A_TLS_STREAM" \
  --tls-cert-path "$TLS_CERT" \
  --tls-key-path "$TLS_KEY" \
  --evidence-file "$LOG_DIR/session-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port session-a "$SESSION_A_HTTP"
wait_port session-a-control "$SESSION_A_CONTROL"
wait_port session-a-spot-router "$SESSION_A_SPOT_ROUTER"
wait_port session-a-stream "$SESSION_A_STREAM"
wait_port session-a-tls-stream "$SESSION_A_TLS_STREAM"

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

run_client() {
  local scenario_set="$1"
  echo "client scenario_set=${scenario_set}" >>"$LOG_DIR/client.stdout.log"
  dotnet "$CLIENT_DLL" \
    --session-a-stream-endpoint "$SESSION_A_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --session-a-tls-stream-endpoint "$SESSION_A_TLS_STREAM" \
    --registry-router-endpoint "$REGISTRY_ROUTER" \
    --play-a-evidence-url "$PLAY_A_HTTP/evidence" \
    --play-b-evidence-url "$PLAY_B_HTTP/evidence" \
    --session-a-evidence-url "$SESSION_A_HTTP/evidence" \
    --play-a-crash-url "$PLAY_A_HTTP/crash" \
    --scenario-set "$scenario_set" \
    --play-a-rid play-a \
    --play-b-rid play-b \
    --session-a-rid session-a \
    --play-a-external-spot-endpoint "$PLAY_A_EXTERNAL_SPOT" \
    --play-b-external-spot-endpoint "$PLAY_B_EXTERNAL_SPOT" \
    --play-a-spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
    --client-control-endpoint "$CLIENT_CONTROL" \
    --client-external-route-endpoint "$CLIENT_EXTERNAL_ROUTE" \
    --client-external-route-b-endpoint "$CLIENT_EXTERNAL_ROUTE_B" \
    --client-external-channel-endpoint "$CLIENT_EXTERNAL_CHANNEL" \
    --client-spot-router-endpoint "$CLIENT_SPOT_ROUTER" \
    --client-spot-pub-endpoint "$CLIENT_SPOT_PUB" \
    --log-dir "$LOG_DIR" \
    >>"$LOG_DIR/client.stdout.log" 2>>"$LOG_DIR/client.stderr.log"
}

if [[ "$SCENARIO_SET" == "track-g" ]]; then
  run_client sm-g2
  run_client sm-g3
  run_client sm-g4
  run_client sm-g1
elif [[ "$SCENARIO_SET" == "all" ]]; then
  run_client baseline-1
  run_client track-c
  run_client baseline-2a
  run_client sm-e4
  run_client baseline-2b
  run_client sm-g2
  run_client sm-g3
  run_client sm-g4
  run_client sm-g1
else
  run_client "$SCENARIO_SET"
fi

cat "$LOG_DIR/client.stdout.log"
