#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

pick_port() {
  node -e "const net=require('node:net'); const s=net.createServer(); s.listen(0,'127.0.0.1',()=>{console.log(s.address().port); s.close();});"
}

build_package() {
  local dir="$1"
  (cd "$dir" && npm run build >/dev/null)
}

build_tsc_package() {
  local dir="$1"
  (cd "$dir" && node ../../../../../bindings/node/node_modules/typescript/bin/tsc -p tsconfig.json)
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
        tail -n 100 "$file" >&2 || true
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
  ZLINK_E2E_RID="$name" node "$main" "$@" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_tsc_package "$NODE_ROOT/packages/framework-codec-protobuf"
build_tsc_package "$NODE_ROOT/packages/framework-codec-msgpack"
build_package "$ROOT_DIR/Server/Main"
build_package "$ROOT_DIR/Server/InvalidDuplicate"
build_package "$ROOT_DIR/Server/JsonOnlyPeer"
build_package "$ROOT_DIR/Server/CodecRequester"
build_package "$ROOT_DIR/Client"

MAIN_HTTP_PORT="$(pick_port)"
JSON_ONLY_HTTP_PORT="$(pick_port)"
CODEC_REQUESTER_HTTP_PORT="$(pick_port)"
INVALID_HTTP_PORT="$(pick_port)"
MAIN_CHANNEL_PORT="$(pick_port)"
JSON_ONLY_CHANNEL_PORT="$(pick_port)"
INVALID_CHANNEL_PORT="$(pick_port)"

MAIN_URL="http://127.0.0.1:$MAIN_HTTP_PORT"
JSON_ONLY_URL="http://127.0.0.1:$JSON_ONLY_HTTP_PORT"
CODEC_REQUESTER_URL="http://127.0.0.1:$CODEC_REQUESTER_HTTP_PORT"
INVALID_URL="http://127.0.0.1:$INVALID_HTTP_PORT"
MAIN_CHANNEL="tcp://127.0.0.1:$MAIN_CHANNEL_PORT"
JSON_ONLY_CHANNEL="tcp://127.0.0.1:$JSON_ONLY_CHANNEL_PORT"
INVALID_CHANNEL="tcp://127.0.0.1:$INVALID_CHANNEL_PORT"

MAIN_MAIN="$ROOT_DIR/Server/Main/dist/Server/Main/main.js"
INVALID_MAIN="$ROOT_DIR/Server/InvalidDuplicate/dist/Server/InvalidDuplicate/main.js"
JSON_ONLY_MAIN="$ROOT_DIR/Server/JsonOnlyPeer/dist/Server/JsonOnlyPeer/main.js"
CODEC_REQUESTER_MAIN="$ROOT_DIR/Server/CodecRequester/dist/Server/CodecRequester/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_server reg-codec-node "$MAIN_MAIN" \
  --rid reg-codec-node \
  --http-url "$MAIN_URL" \
  --channel-endpoint "$MAIN_CHANNEL" \
  --evidence-file "$LOG_DIR/server.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$MAIN_URL" reg-codec-node

start_server json-only-peer "$JSON_ONLY_MAIN" \
  --rid json-only-peer \
  --http-url "$JSON_ONLY_URL" \
  --channel-endpoint "$JSON_ONLY_CHANNEL" \
  --evidence-file "$LOG_DIR/json-only.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$JSON_ONLY_URL" json-only-peer

start_server codec-requester "$CODEC_REQUESTER_MAIN" \
  --rid codec-requester \
  --http-url "$CODEC_REQUESTER_URL" \
  --target-endpoint "$JSON_ONLY_CHANNEL" \
  --log-dir "$LOG_DIR"
wait_health "$CODEC_REQUESTER_URL" codec-requester

node "$CLIENT_MAIN" \
  --server-url "$MAIN_URL" \
  --json-only-url "$JSON_ONLY_URL" \
  --codec-requester-url "$CODEC_REQUESTER_URL" \
  --invalid-main "$INVALID_MAIN" \
  --invalid-http-url "$INVALID_URL" \
  --invalid-channel-endpoint "$INVALID_CHANNEL" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
