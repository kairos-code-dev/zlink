#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
HTTP_PROBE_TIMEOUT_SECONDS=3
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

used_ports=()
allocate_port() {
  local port
  while true; do
    port="$(pick_port)"
    local used=0
    for existing in "${used_ports[@]:-}"; do
      if [[ "$existing" == "$port" ]]; then
        used=1
        break
      fi
    done
    if [[ "$used" -eq 0 ]]; then
      used_ports+=("$port")
      printf '%s\n' "$port"
      return 0
    fi
  done
}

build_package() {
  local dir="$1"
  rm -rf "$dir/dist"
  (cd "$dir" && npm run build >/dev/null)
}

wait_health() {
  local url="$1"
  local name="$2"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if curl --max-time "${HTTP_PROBE_TIMEOUT_SECONDS}" -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for $name at $url" >&2
  return 1
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local address="${endpoint#*://}"
  local host="${address%:*}"
  local port="${address##*:}"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if node -e "const net=require('node:net'); const s=net.connect({host: process.argv[1], port: Number(process.argv[2])}, () => { s.end(); process.exit(0); }); s.on('error', () => process.exit(1)); setTimeout(() => process.exit(1), 100);" "$host" "$port" >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for $name at $endpoint" >&2
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

ordered_roles() {
  python3 - "$E2E_START_ORDER" "$@" <<'PY'
import random
import sys

mode = sys.argv[1]
roles = sys.argv[2:]
if mode in ("", "forward"):
    pass
elif mode == "reverse":
    roles.reverse()
elif mode.startswith("shuffle:"):
    seed_text = mode.split(":", 1)[1]
    if seed_text == "":
        raise SystemExit("E2E_START_ORDER shuffle requires a seed")
    random.Random(int(seed_text)).shuffle(roles)
else:
    raise SystemExit(f"unsupported E2E_START_ORDER={mode!r}")
for role in roles:
    print(role)
PY
}

echo "log_dir=$LOG_DIR"
echo "start_order=$E2E_START_ORDER"

if [[ "$SCENARIO" == "all" && "${ZLINK_SPOT_SERVICE_ALL_CHILD:-0}" != "1" ]]; then
  for child_group in default-batch SM-G2 SM-G3 SM-G4 SM-G1 SM-Q9; do
    echo "child scenario=$child_group"
    if ! timeout 420s env ZLINK_SPOT_SERVICE_ALL_CHILD=1 "$0" "$child_group"; then
      echo "child scenario=$child_group failed" >&2
      exit 1
    fi
  done
  echo "spot-service e2e result=passed"
  exit 0
fi

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/Play"
build_package "$ROOT_DIR/Server/Session"
build_package "$ROOT_DIR/Server/Gateway"
build_package "$ROOT_DIR/Server/MultiNode"
build_package "$ROOT_DIR/Client"

PLAY_A_HTTP_PORT="$(allocate_port)"
PLAY_B_HTTP_PORT="$(allocate_port)"
SESSION_A_HTTP_PORT="$(allocate_port)"
SESSION_B_HTTP_PORT="$(allocate_port)"
GATEWAY_HTTP_PORT="$(allocate_port)"
MULTI_A_HTTP_PORT="$(allocate_port)"
MULTI_B_HTTP_PORT="$(allocate_port)"
PLAY_A_CONTROL_PORT="$(allocate_port)"
PLAY_B_CONTROL_PORT="$(allocate_port)"
PLAY_A_EXTERNAL_SPOT_PORT="$(allocate_port)"
PLAY_B_EXTERNAL_SPOT_PORT="$(allocate_port)"
SESSION_A_CONTROL_PORT="$(allocate_port)"
SESSION_B_CONTROL_PORT="$(allocate_port)"
PLAY_A_ROUTER_PORT="$(allocate_port)"
PLAY_B_ROUTER_PORT="$(allocate_port)"
SESSION_A_ROUTER_PORT="$(allocate_port)"
SESSION_B_ROUTER_PORT="$(allocate_port)"
GATEWAY_ROUTER_PORT="$(allocate_port)"
MULTI_A_ROUTE_PORT="$(allocate_port)"
MULTI_B_ROUTE_PORT="$(allocate_port)"
PLAY_A_SPOT_PUB_PORT="$(allocate_port)"
PLAY_B_SPOT_PUB_PORT="$(allocate_port)"
GATEWAY_SPOT_PUB_PORT="$(allocate_port)"
PLAY_A_EXTERNAL_CLIENT_PORT="$(allocate_port)"
PLAY_B_EXTERNAL_CLIENT_PORT="$(allocate_port)"
SESSION_A_STREAM_PORT="$(allocate_port)"
SESSION_A_TLS_STREAM_PORT="$(allocate_port)"
SESSION_B_STREAM_PORT="$(allocate_port)"
MULTI_A_SPOT_ROUTER_PORT="$(allocate_port)"
MULTI_B_SPOT_ROUTER_PORT="$(allocate_port)"

PLAY_A_URL="http://127.0.0.1:$PLAY_A_HTTP_PORT"
PLAY_B_URL="http://127.0.0.1:$PLAY_B_HTTP_PORT"
SESSION_A_URL="http://127.0.0.1:$SESSION_A_HTTP_PORT"
SESSION_B_URL="http://127.0.0.1:$SESSION_B_HTTP_PORT"
GATEWAY_URL="http://127.0.0.1:$GATEWAY_HTTP_PORT"
MULTI_A_URL="http://127.0.0.1:$MULTI_A_HTTP_PORT"
MULTI_B_URL="http://127.0.0.1:$MULTI_B_HTTP_PORT"
PLAY_A_CONTROL="tcp://127.0.0.1:$PLAY_A_CONTROL_PORT"
PLAY_B_CONTROL="tcp://127.0.0.1:$PLAY_B_CONTROL_PORT"
PLAY_A_EXTERNAL_SPOT="tcp://127.0.0.1:$PLAY_A_EXTERNAL_SPOT_PORT"
PLAY_B_EXTERNAL_SPOT="tcp://127.0.0.1:$PLAY_B_EXTERNAL_SPOT_PORT"
SESSION_A_CONTROL="tcp://127.0.0.1:$SESSION_A_CONTROL_PORT"
SESSION_B_CONTROL="tcp://127.0.0.1:$SESSION_B_CONTROL_PORT"
PLAY_A_ROUTER="tcp://127.0.0.1:$PLAY_A_ROUTER_PORT"
PLAY_B_ROUTER="tcp://127.0.0.1:$PLAY_B_ROUTER_PORT"
SESSION_A_ROUTER="tcp://127.0.0.1:$SESSION_A_ROUTER_PORT"
SESSION_B_ROUTER="tcp://127.0.0.1:$SESSION_B_ROUTER_PORT"
GATEWAY_ROUTER="tcp://127.0.0.1:$GATEWAY_ROUTER_PORT"
MULTI_A_ROUTE="tcp://127.0.0.1:$MULTI_A_ROUTE_PORT"
MULTI_B_ROUTE="tcp://127.0.0.1:$MULTI_B_ROUTE_PORT"
PLAY_A_SPOT_PUB="tcp://127.0.0.1:$PLAY_A_SPOT_PUB_PORT"
PLAY_B_SPOT_PUB="tcp://127.0.0.1:$PLAY_B_SPOT_PUB_PORT"
GATEWAY_SPOT_PUB="tcp://127.0.0.1:$GATEWAY_SPOT_PUB_PORT"
PLAY_A_EXTERNAL_CLIENT="tcp://127.0.0.1:$PLAY_A_EXTERNAL_CLIENT_PORT"
PLAY_B_EXTERNAL_CLIENT="tcp://127.0.0.1:$PLAY_B_EXTERNAL_CLIENT_PORT"
SESSION_A_STREAM="tcp://127.0.0.1:$SESSION_A_STREAM_PORT"
SESSION_A_TLS_STREAM="tls://127.0.0.1:$SESSION_A_TLS_STREAM_PORT"
SESSION_B_STREAM="tcp://127.0.0.1:$SESSION_B_STREAM_PORT"
MULTI_A_SPOT_ROUTER="tcp://127.0.0.1:$MULTI_A_SPOT_ROUTER_PORT"
MULTI_B_SPOT_ROUTER="tcp://127.0.0.1:$MULTI_B_SPOT_ROUTER_PORT"

PLAY_MAIN="$ROOT_DIR/Server/Play/dist/Server/Play/main.js"
SESSION_MAIN="$ROOT_DIR/Server/Session/dist/Server/Session/main.js"
GATEWAY_MAIN="$ROOT_DIR/Server/Gateway/dist/Server/Gateway/main.js"
MULTI_NODE_MAIN="$ROOT_DIR/Server/MultiNode/dist/Server/MultiNode/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

TLS_CERT="$LOG_DIR/session-a-tls.crt"
TLS_KEY="$LOG_DIR/session-a-tls.key"
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$TLS_KEY" \
  -out "$TLS_CERT" \
  -subj "/CN=localhost" \
  -days 1 >/dev/null 2>&1

start_named_server() {
  case "$1" in
    play-a)
      start_server play-a "$PLAY_MAIN" \
        --rid play-a \
        --http-url "$PLAY_A_URL" \
        --control-router-endpoint "$PLAY_A_CONTROL" \
        --external-spot-endpoint "$PLAY_A_EXTERNAL_SPOT" \
        --spot-router-endpoint "$PLAY_A_ROUTER" \
        --spot-pub-endpoint "$PLAY_A_SPOT_PUB" \
        --client-spot-pub-endpoint "$GATEWAY_SPOT_PUB" \
        --external-client-endpoint "$PLAY_A_EXTERNAL_CLIENT" \
        --evidence-file "$LOG_DIR/play-a.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    play-b)
      start_server play-b "$PLAY_MAIN" \
        --rid play-b \
        --http-url "$PLAY_B_URL" \
        --control-router-endpoint "$PLAY_B_CONTROL" \
        --external-spot-endpoint "$PLAY_B_EXTERNAL_SPOT" \
        --play-a-external-spot-endpoint "$PLAY_A_EXTERNAL_SPOT" \
        --spot-router-endpoint "$PLAY_B_ROUTER" \
        --spot-pub-endpoint "$PLAY_B_SPOT_PUB" \
        --external-client-endpoint "$PLAY_B_EXTERNAL_CLIENT" \
        --evidence-file "$LOG_DIR/play-b.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    session-a)
      start_server session-a "$SESSION_MAIN" \
        --rid session-a \
        --http-url "$SESSION_A_URL" \
        --control-router-endpoint "$SESSION_A_CONTROL" \
        --play-control-endpoint "$PLAY_A_CONTROL,$PLAY_B_CONTROL" \
        --spot-router-endpoint "$SESSION_A_ROUTER" \
        --play-spot-router-play-a "$PLAY_A_ROUTER" \
        --play-spot-router-play-b "$PLAY_B_ROUTER" \
        --stream-endpoint "$SESSION_A_STREAM" \
        --tls-stream-endpoint "$SESSION_A_TLS_STREAM" \
        --tls-cert-path "$TLS_CERT" \
        --tls-key-path "$TLS_KEY" \
        --evidence-file "$LOG_DIR/session-a.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    session-b)
      start_server session-b "$SESSION_MAIN" \
        --rid session-b \
        --http-url "$SESSION_B_URL" \
        --control-router-endpoint "$SESSION_B_CONTROL" \
        --play-control-endpoint "$PLAY_A_CONTROL,$PLAY_B_CONTROL" \
        --spot-router-endpoint "$SESSION_B_ROUTER" \
        --play-spot-router-play-a "$PLAY_A_ROUTER" \
        --play-spot-router-play-b "$PLAY_B_ROUTER" \
        --stream-endpoint "$SESSION_B_STREAM" \
        --evidence-file "$LOG_DIR/session-b.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    gateway)
      start_server gateway "$GATEWAY_MAIN" \
        --rid gateway \
        --http-url "$GATEWAY_URL" \
        --spot-router-endpoint "$GATEWAY_ROUTER" \
        --spot-pub-endpoint "$GATEWAY_SPOT_PUB" \
        --spot-pub-peer "$PLAY_A_SPOT_PUB,$PLAY_B_SPOT_PUB" \
        --evidence-file "$LOG_DIR/gateway.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    multi-node-a)
      start_server multi-node-a "$MULTI_NODE_MAIN" \
        --rid multi-node-a \
        --http-url "$MULTI_A_URL" \
        --route-endpoint "$MULTI_A_ROUTE" \
        --spot-router-endpoint "$MULTI_A_SPOT_ROUTER" \
        --evidence-file "$LOG_DIR/multi-node-a.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    multi-node-b)
      start_server multi-node-b "$MULTI_NODE_MAIN" \
        --rid multi-node-b \
        --http-url "$MULTI_B_URL" \
        --route-endpoint "$MULTI_B_ROUTE" \
        --spot-router-endpoint "$MULTI_B_SPOT_ROUTER" \
        --evidence-file "$LOG_DIR/multi-node-b.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

wait_named_server() {
  case "$1" in
    play-a) wait_health "$PLAY_A_URL" play-a ;;
    play-b) wait_health "$PLAY_B_URL" play-b ;;
    session-a)
      wait_health "$SESSION_A_URL" session-a
      wait_port session-a-tls-stream "$SESSION_A_TLS_STREAM"
      ;;
    session-b) wait_health "$SESSION_B_URL" session-b ;;
    gateway) wait_health "$GATEWAY_URL" gateway ;;
    multi-node-a) wait_health "$MULTI_A_URL" multi-node-a ;;
    multi-node-b) wait_health "$MULTI_B_URL" multi-node-b ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

SERVER_ROLES=(play-a play-b session-a session-b gateway multi-node-a multi-node-b)
mapfile -t ORDERED_SERVER_ROLES < <(ordered_roles "${SERVER_ROLES[@]}")
for role in "${ORDERED_SERVER_ROLES[@]}"; do
  start_named_server "$role"
done
for role in "${SERVER_ROLES[@]}"; do
  wait_named_server "$role"
done

run_client() {
  local scenario="$1"
  echo "client scenario=${scenario}" >>"$LOG_DIR/client.stdout.log"
  node "$CLIENT_MAIN" \
    --play-a-url "$PLAY_A_URL" \
    --play-b-url "$PLAY_B_URL" \
    --gateway-url "$GATEWAY_URL" \
    --session-a-url "$SESSION_A_URL" \
    --session-b-url "$SESSION_B_URL" \
    --session-a-stream-endpoint "$SESSION_A_STREAM" \
    --session-a-tls-stream-endpoint "$SESSION_A_TLS_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --multi-a-url "$MULTI_A_URL" \
    --multi-b-url "$MULTI_B_URL" \
    --scenario "$scenario" \
    >>"$LOG_DIR/client.stdout.log" 2>>"$LOG_DIR/client.stderr.log"
}

if [[ "$SCENARIO" == "default-batch" ]]; then
  run_client sm-b1-b2-b3-b5
  run_client sm-b6
  run_client sm-b8
  run_client sm-d1-d6
  run_client sm-d3
  run_client sm-d4
  run_client sm-d5
  run_client sm-d7
  run_client sm-d8
  run_client sm-d9-d11-d13
  run_client sm-d10
  run_client sm-d12
  run_client sm-d14
  run_client sm-c1-c2
  run_client sm-c3
  run_client sm-e4
  run_client sm-e1-f4
  run_client sm-e2-e3
  run_client sm-a7-a8-c4
  run_client sm-a3-a6-b4-b7
  run_client sm-a5
  run_client sm-a1-a2-a4-f1-f2
else
  run_client "$SCENARIO"
fi

cat "$LOG_DIR/client.stdout.log"
