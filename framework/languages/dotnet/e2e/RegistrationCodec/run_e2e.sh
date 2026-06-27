#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

SERVER_PROJECT="$ROOT_DIR/Server/Main/RegistrationCodec.Server.csproj"
INVALID_SERVER_PROJECT="$ROOT_DIR/Server/InvalidDuplicate/RegistrationCodec.InvalidDuplicate.csproj"
JSON_ONLY_PEER_PROJECT="$ROOT_DIR/Server/JsonOnlyPeer/RegistrationCodec.JsonOnlyPeer.csproj"
CODEC_REQUESTER_PROJECT="$ROOT_DIR/Server/CodecRequester/RegistrationCodec.CodecRequester.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/RegistrationCodec.Client.csproj"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

SERVER_HTTP_PORT="$(pick_port)"
CHANNEL_PORT="$(pick_port)"
JSON_ONLY_HTTP_PORT="$(pick_port)"
JSON_ONLY_CHANNEL_PORT="$(pick_port)"
CODEC_REQUESTER_HTTP_PORT="$(pick_port)"
SERVER_URL="http://127.0.0.1:$SERVER_HTTP_PORT"
CHANNEL_ENDPOINT="tcp://127.0.0.1:$CHANNEL_PORT"
JSON_ONLY_URL="http://127.0.0.1:$JSON_ONLY_HTTP_PORT"
JSON_ONLY_CHANNEL_ENDPOINT="tcp://127.0.0.1:$JSON_ONLY_CHANNEL_PORT"
CODEC_REQUESTER_URL="http://127.0.0.1:$CODEC_REQUESTER_HTTP_PORT"

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
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

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

echo "log_dir=$LOG_DIR"

dotnet run --project "$SERVER_PROJECT" -- \
  --rid reg-codec-node \
  --http-url "$SERVER_URL" \
  --channel-endpoint "$CHANNEL_ENDPOINT" \
  --evidence-file "$LOG_DIR/server.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/server.stdout.log" 2>"$LOG_DIR/server.stderr.log" &
pids+=("$!")
wait_health "$SERVER_URL" server

dotnet run --project "$JSON_ONLY_PEER_PROJECT" -- \
  --rid codec-mismatch-json-only \
  --http-url "$JSON_ONLY_URL" \
  --channel-endpoint "$JSON_ONLY_CHANNEL_ENDPOINT" \
  --evidence-file "$LOG_DIR/codec-mismatch-json-only.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/codec-mismatch-json-only.stdout.log" 2>"$LOG_DIR/codec-mismatch-json-only.stderr.log" &
pids+=("$!")
wait_health "$JSON_ONLY_URL" codec-mismatch-json-only

dotnet run --project "$CODEC_REQUESTER_PROJECT" -- \
  --rid codec-mismatch-requester \
  --http-url "$CODEC_REQUESTER_URL" \
  --channel-endpoint "$JSON_ONLY_CHANNEL_ENDPOINT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/codec-mismatch-requester.stdout.log" 2>"$LOG_DIR/codec-mismatch-requester.stderr.log" &
pids+=("$!")
wait_health "$CODEC_REQUESTER_URL" codec-mismatch-requester

dotnet run --project "$CLIENT_PROJECT" -- \
  --channel-endpoint "$CHANNEL_ENDPOINT" \
  --server-url "$SERVER_URL" \
  --codec-requester-url "$CODEC_REQUESTER_URL" \
  --invalid-server-project "$INVALID_SERVER_PROJECT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
