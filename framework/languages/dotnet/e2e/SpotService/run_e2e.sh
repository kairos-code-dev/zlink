#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REGISTRY_PROJECT="$SCRIPT_DIR/Server/Registry/SpotService.Registry.csproj"
PLAY_PROJECT="$SCRIPT_DIR/Server/Play/SpotService.Play.csproj"
SESSION_PROJECT="$SCRIPT_DIR/Server/Session/SpotService.Session.csproj"
MULTI_NODE_PROJECT="$SCRIPT_DIR/Server/MultiNode/SpotService.MultiNode.csproj"
DRIVER_PROJECT="$SCRIPT_DIR/Server/Driver/SpotService.Driver.csproj"
CLIENT_PROJECT="$SCRIPT_DIR/Client/SpotService.Client.csproj"
REGISTRY_DLL="$SCRIPT_DIR/Server/Registry/bin/Debug/net8.0/SpotService.Registry.dll"
PLAY_DLL="$SCRIPT_DIR/Server/Play/bin/Debug/net8.0/SpotService.Play.dll"
SESSION_DLL="$SCRIPT_DIR/Server/Session/bin/Debug/net8.0/SpotService.Session.dll"
MULTI_NODE_DLL="$SCRIPT_DIR/Server/MultiNode/bin/Debug/net8.0/SpotService.MultiNode.dll"
DRIVER_DLL="$SCRIPT_DIR/Server/Driver/bin/Debug/net8.0/SpotService.Driver.dll"
CLIENT_DLL="$SCRIPT_DIR/Client/bin/Debug/net8.0/SpotService.Client.dll"
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$STAMP"
SCENARIO_SET="${SCENARIO_SET:-all}"
NEED_SESSION_NODES=1
NEED_PLAY_B=1
case "$SCENARIO_SET" in
  track-c|sm-e1-f4|sm-e2-e3|sm-a7-a8-c4|sm-e4|sm-a5)
    NEED_SESSION_NODES=0
    NEED_PLAY_B=0
    ;;
esac
mkdir -p "$LOG_DIR"

if [[ "$SCENARIO_SET" == "all" && "${ZLINK_SPOT_SERVICE_ALL_CHILD:-0}" != "1" ]]; then
  echo "log_dir=$LOG_DIR"
  for child_set in baseline-1 track-c sm-q9 sm-e1-f4 sm-e2-e3 sm-a7-a8-c4 sm-e4 sm-a3-a6-b4-b7 sm-a5 sm-a1-a2-a4-f1-f2 sm-g2 sm-g3 sm-g4 sm-g1; do
    child_ok=0
    for attempt in 1 2; do
      echo "child scenario_set=${child_set} attempt=${attempt}"
      if timeout "${ZLINK_SPOT_SERVICE_CHILD_TIMEOUT:-180s}" \
        env SCENARIO_SET="$child_set" ZLINK_SPOT_SERVICE_ALL_CHILD=1 "$0"; then
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
    while len(sockets) < 37:
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
MULTI_A_HTTP="http://127.0.0.1:${PORTS[28]}"
MULTI_ROUTE_A="tcp://127.0.0.1:${PORTS[29]}"
MULTI_ROUTE_B="tcp://127.0.0.1:${PORTS[30]}"
MULTI_SPOT_ROUTER_A="tcp://127.0.0.1:${PORTS[31]}"
MULTI_SPOT_ROUTER_B="tcp://127.0.0.1:${PORTS[32]}"
CLIENT_MULTI_ROUTE_A="tcp://127.0.0.1:${PORTS[33]}"
CLIENT_MULTI_ROUTE_B="tcp://127.0.0.1:${PORTS[34]}"
MULTI_B_HTTP="http://127.0.0.1:${PORTS[35]}"
DRIVER_HTTP="http://127.0.0.1:${PORTS[36]}"

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
  local attempts="${ZLINK_SPOT_SERVICE_WAIT_PORT_ATTEMPTS:-200}"
  for _ in $(seq 1 "$attempts"); do
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
  local dll="$2"
  shift 2
  dotnet "$dll" "$@" \
    >"$LOG_DIR/${name}.stdout.log" 2>"$LOG_DIR/${name}.stderr.log" &
  PIDS+=("$!")
}

echo "log_dir=$LOG_DIR"
dotnet build "$REGISTRY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$PLAY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$SESSION_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$MULTI_NODE_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$DRIVER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null
TLS_CERT="$LOG_DIR/session-a-tls.crt"
TLS_KEY="$LOG_DIR/session-a-tls.key"
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$TLS_KEY" \
  -out "$TLS_CERT" \
  -days 1 \
  -subj "/CN=localhost" >/dev/null 2>&1

start_server registry "$REGISTRY_DLL" \
  --rid registry \
  --http-url "$REGISTRY_HTTP" \
  --registry-pub-endpoint "$REGISTRY_PUB" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --log-dir "$LOG_DIR"
wait_port registry "$REGISTRY_HTTP"
wait_port registry-router "$REGISTRY_ROUTER"

if [[ "$SCENARIO_SET" != "sm-q9" ]]; then
start_server play-a "$PLAY_DLL" \
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

if [[ "$NEED_PLAY_B" == "1" ]]; then
start_server play-b "$PLAY_DLL" \
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
fi

if [[ "$NEED_SESSION_NODES" == "1" ]]; then
start_server session-a "$SESSION_DLL" \
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

start_server session-b "$SESSION_DLL" \
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
fi
fi

if [[ "$SCENARIO_SET" == "sm-q9" ]]; then
start_server multi-node-a "$MULTI_NODE_DLL" \
  --rid multi-node-a \
  --http-url "$MULTI_A_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --multi-route-a-endpoint "$MULTI_ROUTE_A" \
  --multi-spot-router-a-endpoint "$MULTI_SPOT_ROUTER_A" \
  --evidence-file "$LOG_DIR/multi-node-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port multi-node-a "$MULTI_A_HTTP"
wait_port multi-route-a "$MULTI_ROUTE_A"
wait_port multi-spot-router-a "$MULTI_SPOT_ROUTER_A"

start_server multi-node-b "$MULTI_NODE_DLL" \
  --rid multi-node-b \
  --http-url "$MULTI_B_HTTP" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --multi-route-b-endpoint "$MULTI_ROUTE_B" \
  --multi-spot-router-b-endpoint "$MULTI_SPOT_ROUTER_B" \
  --evidence-file "$LOG_DIR/multi-node-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_port multi-node-b "$MULTI_B_HTTP"
wait_port multi-route-b "$MULTI_ROUTE_B"
wait_port multi-spot-router-b "$MULTI_SPOT_ROUTER_B"
fi

sleep 2

start_server driver "$DRIVER_DLL" \
  --driver-url "$DRIVER_HTTP" \
  --session-a-stream-endpoint "$SESSION_A_STREAM" \
  --session-b-stream-endpoint "$SESSION_B_STREAM" \
  --session-a-tls-stream-endpoint "$SESSION_A_TLS_STREAM" \
  --registry-router-endpoint "$REGISTRY_ROUTER" \
  --play-a-evidence-url "$PLAY_A_HTTP/evidence" \
  --play-b-evidence-url "$PLAY_B_HTTP/evidence" \
  --session-a-evidence-url "$SESSION_A_HTTP/evidence" \
  --play-a-crash-url "$PLAY_A_HTTP/crash" \
  --multi-evidence-url "$MULTI_A_HTTP/evidence" \
  --multi-b-evidence-url "$MULTI_B_HTTP/evidence" \
  --play-a-rid play-a \
  --play-b-rid play-b \
  --session-a-rid session-a \
  --play-a-control-endpoint "$PLAY_A_CONTROL" \
  --play-b-control-endpoint "$PLAY_B_CONTROL" \
  --play-a-external-spot-endpoint "$PLAY_A_EXTERNAL_SPOT" \
  --play-b-external-spot-endpoint "$PLAY_B_EXTERNAL_SPOT" \
  --play-a-spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
  --client-control-endpoint "$CLIENT_CONTROL" \
  --client-external-route-endpoint "$CLIENT_EXTERNAL_ROUTE" \
  --client-external-route-b-endpoint "$CLIENT_EXTERNAL_ROUTE_B" \
  --client-external-channel-endpoint "$CLIENT_EXTERNAL_CHANNEL" \
  --client-spot-router-endpoint "$CLIENT_SPOT_ROUTER" \
  --client-spot-pub-endpoint "$CLIENT_SPOT_PUB" \
  --client-multi-route-a-endpoint "$CLIENT_MULTI_ROUTE_A" \
  --client-multi-route-b-endpoint "$CLIENT_MULTI_ROUTE_B" \
  --log-dir "$LOG_DIR"
wait_port driver "$DRIVER_HTTP"

run_client() {
  local scenario_set="$1"
  echo "client scenario_set=${scenario_set}" >>"$LOG_DIR/client.stdout.log"
  timeout "${ZLINK_SPOT_SERVICE_CLIENT_TIMEOUT:-120s}" dotnet "$CLIENT_DLL" \
    --driver-url "$DRIVER_HTTP" \
    --scenario-set "$scenario_set" \
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
  run_client sm-q9
  run_client baseline-2a
  run_client sm-e4
  run_client sm-a3-a6-b4-b7
  run_client sm-a5
  run_client sm-a1-a2-a4-f1-f2
  run_client sm-g2
  run_client sm-g3
  run_client sm-g4
  run_client sm-g1
else
  run_client "$SCENARIO_SET"
fi

cat "$LOG_DIR/client.stdout.log"
