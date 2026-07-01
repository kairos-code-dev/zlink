#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
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

wait_health() {
  local url="$1"
  local name="$2"
  for _ in $(seq 1 120); do
    if curl -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.25
  done
  echo "Timed out waiting for $name at $url" >&2
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
build_package "$ROOT_DIR/Server/Service"
build_package "$ROOT_DIR/Server/FilteredService"
build_package "$ROOT_DIR/Server/ThrowingService"
build_package "$ROOT_DIR/Server/Trigger"
build_package "$ROOT_DIR/Client"

REG_HTTP_PORT="$(pick_port)"
SVC_HTTP_PORT="$(pick_port)"
SVC_B_HTTP_PORT="$(pick_port)"
THROW_HTTP_PORT="$(pick_port)"
TRIGGER_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
CHANNEL_PORT="$(pick_port)"
CHANNEL_B_PORT="$(pick_port)"
THROW_CHANNEL_PORT="$(pick_port)"
SPOT_ROUTER_PORT="$(pick_port)"
SPOT_PUB_PORT="$(pick_port)"
SPOT_B_ROUTER_PORT="$(pick_port)"
SPOT_B_PUB_PORT="$(pick_port)"
THROW_SPOT_ROUTER_PORT="$(pick_port)"
THROW_SPOT_PUB_PORT="$(pick_port)"

REG_URL="http://127.0.0.1:$REG_HTTP_PORT"
SVC_URL="http://127.0.0.1:$SVC_HTTP_PORT"
SVC_B_URL="http://127.0.0.1:$SVC_B_HTTP_PORT"
THROW_URL="http://127.0.0.1:$THROW_HTTP_PORT"
TRIGGER_URL="http://127.0.0.1:$TRIGGER_HTTP_PORT"
REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
CHANNEL_ENDPOINT="tcp://127.0.0.1:$CHANNEL_PORT"
CHANNEL_B_ENDPOINT="tcp://127.0.0.1:$CHANNEL_B_PORT"
THROW_CHANNEL_ENDPOINT="tcp://127.0.0.1:$THROW_CHANNEL_PORT"
SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:$SPOT_ROUTER_PORT"
SPOT_PUB_ENDPOINT="tcp://127.0.0.1:$SPOT_PUB_PORT"
SPOT_B_ROUTER_ENDPOINT="tcp://127.0.0.1:$SPOT_B_ROUTER_PORT"
SPOT_B_PUB_ENDPOINT="tcp://127.0.0.1:$SPOT_B_PUB_PORT"
THROW_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:$THROW_SPOT_ROUTER_PORT"
THROW_SPOT_PUB_ENDPOINT="tcp://127.0.0.1:$THROW_SPOT_PUB_PORT"

REGISTRY_MAIN="$ROOT_DIR/Server/Registry/dist/RuntimeMonitoring/Server/Registry/main.js"
SERVICE_MAIN="$ROOT_DIR/Server/Service/dist/Server/Service/main.js"
FILTERED_SERVICE_MAIN="$ROOT_DIR/Server/FilteredService/dist/Server/FilteredService/main.js"
THROWING_SERVICE_MAIN="$ROOT_DIR/Server/ThrowingService/dist/Server/ThrowingService/main.js"
TRIGGER_MAIN="$ROOT_DIR/Server/Trigger/dist/Server/Trigger/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_server registry "$REGISTRY_MAIN" \
  --rid registry \
  --http-url "$REG_URL" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER" \
  --evidence-file "$LOG_DIR/registry.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$REG_URL" registry

start_server svc-a "$SERVICE_MAIN" \
  --rid svc-a \
  --http-url "$SVC_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$CHANNEL_ENDPOINT" \
  --spot-router-endpoint "$SPOT_ROUTER_ENDPOINT" \
  --spot-pub-endpoint "$SPOT_PUB_ENDPOINT" \
  --evidence-file "$LOG_DIR/svc-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$SVC_URL" svc-a

start_server svc-b "$FILTERED_SERVICE_MAIN" \
  --rid svc-b \
  --http-url "$SVC_B_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$CHANNEL_B_ENDPOINT" \
  --spot-router-endpoint "$SPOT_B_ROUTER_ENDPOINT" \
  --spot-pub-endpoint "$SPOT_B_PUB_ENDPOINT" \
  --evidence-file "$LOG_DIR/svc-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$SVC_B_URL" svc-b

start_server svc-throw "$THROWING_SERVICE_MAIN" \
  --rid svc-throw \
  --http-url "$THROW_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$THROW_CHANNEL_ENDPOINT" \
  --spot-router-endpoint "$THROW_SPOT_ROUTER_ENDPOINT" \
  --spot-pub-endpoint "$THROW_SPOT_PUB_ENDPOINT" \
  --evidence-file "$LOG_DIR/svc-throw.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$THROW_URL" svc-throw

start_server trigger "$TRIGGER_MAIN" \
  --http-url "$TRIGGER_URL" \
  --service-channel-endpoint "$CHANNEL_ENDPOINT" \
  --service-b-channel-endpoint "$CHANNEL_B_ENDPOINT" \
  --throw-channel-endpoint "$THROW_CHANNEL_ENDPOINT" \
  --log-dir "$LOG_DIR"
wait_health "$TRIGGER_URL" trigger

node "$CLIENT_MAIN" \
  --trigger-url "$TRIGGER_URL" \
  --registry-url "$REG_URL" \
  --service-url "$SVC_URL" \
  --service-b-url "$SVC_B_URL" \
  --throw-service-url "$THROW_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --service-b-channel-endpoint "$CHANNEL_B_ENDPOINT" \
  --service-b-spot-router-endpoint "$SPOT_B_ROUTER_ENDPOINT" \
  --service-b-spot-pub-endpoint "$SPOT_B_PUB_ENDPOINT" \
  --service-main "$SERVICE_MAIN" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
