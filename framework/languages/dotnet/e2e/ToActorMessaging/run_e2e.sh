#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

ACTOR_PROJECT="$ROOT_DIR/Server/Actor/ToActorMessaging.Actor.csproj"
CALLER_PROJECT="$ROOT_DIR/Server/Caller/ToActorMessaging.Caller.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/ToActorMessaging.Client.csproj"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

pids=()
REDIS_CONTAINER=""
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    kill "$pid" >/dev/null 2>&1 || true
  done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
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

if [[ -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  REDIS_ENDPOINT="$ZLINK_REDIS_E2E_ENDPOINT"
else
  REDIS_PORT="$(pick_port)"
  REDIS_CONTAINER="zlink-e2e-to-actor-$$"
  docker run -d --rm --name "$REDIS_CONTAINER" -p "$REDIS_PORT:6379" redis:7-alpine >/dev/null
  REDIS_ENDPOINT="127.0.0.1:$REDIS_PORT"
fi
REDIS_KEY_PREFIX="zlink:e2e:to-actor:$(date +%s)-$$"

ACTOR_HTTP_PORT="$(pick_port)"
CALLER_HTTP_PORT="$(pick_port)"
ACTOR_ROUTER_PORT="$(pick_port)"
ACTOR_PUBSUB_PORT="$(pick_port)"
CALLER_ROUTER_PORT="$(pick_port)"
CALLER_PUBSUB_PORT="$(pick_port)"

ACTOR_URL="http://127.0.0.1:$ACTOR_HTTP_PORT"
CALLER_URL="http://127.0.0.1:$CALLER_HTTP_PORT"

echo "log_dir=$LOG_DIR"
dotnet build "$ACTOR_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CALLER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

dotnet run --no-build --project "$ACTOR_PROJECT" -- \
  --rid to-actor-owner \
  --http-url "$ACTOR_URL" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --router-endpoint "tcp://127.0.0.1:$ACTOR_ROUTER_PORT" \
  --pubsub-endpoint "tcp://127.0.0.1:$ACTOR_PUBSUB_PORT" \
  --evidence-file "$LOG_DIR/actor.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/actor.stdout.log" 2>"$LOG_DIR/actor.stderr.log" &
pids+=("$!")

dotnet run --no-build --project "$CALLER_PROJECT" -- \
  --rid to-actor-caller \
  --http-url "$CALLER_URL" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --router-endpoint "tcp://127.0.0.1:$CALLER_ROUTER_PORT" \
  --pubsub-endpoint "tcp://127.0.0.1:$CALLER_PUBSUB_PORT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/caller.stdout.log" 2>"$LOG_DIR/caller.stderr.log" &
pids+=("$!")

wait_health "$ACTOR_URL" actor
wait_health "$CALLER_URL" caller
sleep 5

dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --actor-url "$ACTOR_URL" \
  --caller-url "$CALLER_URL" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
