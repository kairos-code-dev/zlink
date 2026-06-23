#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

SERVER_PROJECT="$ROOT_DIR/Server/PubSub.Server.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/PubSub.Client.csproj"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

REG_HTTP_PORT="$(pick_port)"
PUB_HTTP_PORT="$(pick_port)"
SUB_1_HTTP_PORT="$(pick_port)"
SUB_2_HTTP_PORT="$(pick_port)"
SUB_3_HTTP_PORT="$(pick_port)"
SUB_LATE_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
PUB_PORT="$(pick_port)"

REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
PUB_ENDPOINT="tcp://127.0.0.1:$PUB_PORT"
PUB_URL="http://127.0.0.1:$PUB_HTTP_PORT"
SUB_1_URL="http://127.0.0.1:$SUB_1_HTTP_PORT"
SUB_2_URL="http://127.0.0.1:$SUB_2_HTTP_PORT"
SUB_3_URL="http://127.0.0.1:$SUB_3_HTTP_PORT"
SUB_LATE_URL="http://127.0.0.1:$SUB_LATE_HTTP_PORT"

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

start_server() {
  local name="$1"
  shift
  ZLINK_E2E_RID="$name" dotnet run --project "$SERVER_PROJECT" -- "$@" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"

start_server registry \
  --role registry \
  --rid registry \
  --http-url "http://127.0.0.1:$REG_HTTP_PORT" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$REG_HTTP_PORT" registry

start_server pub-a \
  --role publisher \
  --rid pub-a \
  --http-url "$PUB_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --publisher-endpoint "$PUB_ENDPOINT" \
  --evidence-file "$LOG_DIR/pub-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$PUB_URL" pub-a

for sub in 1 2 3; do
  url_var="SUB_${sub}_URL"
  extra_args=()
  if [[ "$sub" == "3" ]]; then
    extra_args+=(--handler-delay-ms 3000)
  fi
  start_server "sub-$sub" \
    --role subscriber \
    --rid "sub-$sub" \
    --http-url "${!url_var}" \
    --registry-router-endpoint "$REG_ROUTER" \
    --publisher-endpoint "$PUB_ENDPOINT" \
    --evidence-file "$LOG_DIR/sub-$sub.evidence.log" \
    --log-dir "$LOG_DIR" \
    "${extra_args[@]}"
  wait_health "${!url_var}" "sub-$sub"
done

dotnet run --project "$CLIENT_PROJECT" -- \
  --publisher-url "$PUB_URL" \
  --subscriber-url "$SUB_1_URL" \
  --subscriber-url "$SUB_2_URL" \
  --subscriber-url "$SUB_3_URL" \
  --late-subscriber-url "$SUB_LATE_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --publisher-endpoint "$PUB_ENDPOINT" \
  --server-project "$SERVER_PROJECT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
