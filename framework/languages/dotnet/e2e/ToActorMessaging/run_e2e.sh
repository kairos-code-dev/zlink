#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

ACTOR_PROJECT="$ROOT_DIR/Server/Actor/ToActorMessaging.Actor.csproj"
CALLER_PROJECT="$ROOT_DIR/Server/Caller/ToActorMessaging.Caller.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/ToActorMessaging.Client.csproj"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"

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
    kill -- "-$pid" >/dev/null 2>&1 || kill "$pid" >/dev/null 2>&1 || true
  done
  sleep 0.5
  for pid in "${pids[@]:-}"; do
    kill -KILL -- "-$pid" >/dev/null 2>&1 || kill -KILL "$pid" >/dev/null 2>&1 || true
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

start_actor() {
  setsid dotnet run --no-build --project "$ACTOR_PROJECT" -- \
    --rid "$ACTOR_RID" \
    --http-url "$ACTOR_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "tcp://127.0.0.1:$ACTOR_ROUTER_PORT" \
    --pubsub-endpoint "tcp://127.0.0.1:$ACTOR_PUBSUB_PORT" \
    --evidence-file "$LOG_DIR/actor.evidence.log" \
    --log-dir "$LOG_DIR" \
    >"$LOG_DIR/actor.stdout.log" 2>"$LOG_DIR/actor.stderr.log" &
  pids+=("$!")
}

start_caller() {
  setsid dotnet run --no-build --project "$CALLER_PROJECT" -- \
    --rid "$CALLER_RID" \
    --http-url "$CALLER_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "tcp://127.0.0.1:$CALLER_ROUTER_PORT" \
    --pubsub-endpoint "tcp://127.0.0.1:$CALLER_PUBSUB_PORT" \
    --log-dir "$LOG_DIR" \
    >"$LOG_DIR/caller.stdout.log" 2>"$LOG_DIR/caller.stderr.log" &
  pids+=("$!")
}

start_role() {
  case "$1" in
    actor) start_actor ;;
    caller) start_caller ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
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
ACTOR_RID="${TO_ACTOR_ACTOR_RID:-to-actor-owner}"
CALLER_RID="${TO_ACTOR_CALLER_RID:-to-actor-caller}"

ACTOR_HTTP_PORT="$(pick_port)"
CALLER_HTTP_PORT="$(pick_port)"
ACTOR_ROUTER_PORT="$(pick_port)"
ACTOR_PUBSUB_PORT="$(pick_port)"
CALLER_ROUTER_PORT="$(pick_port)"
CALLER_PUBSUB_PORT="$(pick_port)"

ACTOR_URL="http://127.0.0.1:$ACTOR_HTTP_PORT"
CALLER_URL="http://127.0.0.1:$CALLER_HTTP_PORT"

echo "log_dir=$LOG_DIR"
echo "start_order=$E2E_START_ORDER"
dotnet build "$ACTOR_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CALLER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

mapfile -t SERVER_ROLES < <(ordered_roles actor caller)
for role in "${SERVER_ROLES[@]}"; do
  start_role "$role"
done

wait_health "$ACTOR_URL" actor
wait_health "$CALLER_URL" caller
sleep 5

dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --actor-url "$ACTOR_URL" \
  --caller-url "$CALLER_URL" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
