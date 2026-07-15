#!/usr/bin/env bash
set -euo pipefail
umask 077

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
if [[ "$#" -eq 0 ]]; then
  SCENARIO="all"
else
  SCENARIO="$*"
  SCENARIO="${SCENARIO// /,}"
fi
mkdir -p "$LOG_DIR"
CONFIG_DIR="$(mktemp -d)"
LOCAL_READINESS_TIMEOUT_SECONDS="${ZLINK_DOTNET_E2E_READY_TIMEOUT_SECONDS:-3}"
LOCAL_READINESS_POLL_SECONDS=0.1
REDIS_READINESS_TIMEOUT_SECONDS="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"

PUBLISHER_PROJECT="$ROOT_DIR/Server/Publisher/PubSub.Publisher.csproj"
SUBSCRIBER_PROJECT="$ROOT_DIR/Server/Subscriber/PubSub.Subscriber.csproj"
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

PUB_HTTP_PORT="$(pick_port)"
SUB_1_HTTP_PORT="$(pick_port)"
SUB_2_HTTP_PORT="$(pick_port)"
SUB_3_HTTP_PORT="$(pick_port)"
SUB_LATE_HTTP_PORT="$(pick_port)"
PUB_PORT="$(pick_port)"

PUB_ENDPOINT="tcp://127.0.0.1:$PUB_PORT"
PUB_URL="http://127.0.0.1:$PUB_HTTP_PORT"
SUB_1_URL="http://127.0.0.1:$SUB_1_HTTP_PORT"
SUB_2_URL="http://127.0.0.1:$SUB_2_HTTP_PORT"
SUB_3_URL="http://127.0.0.1:$SUB_3_HTTP_PORT"
SUB_LATE_URL="http://127.0.0.1:$SUB_LATE_HTTP_PORT"

pids=()
REDIS_CONTAINER=""
cleanup() {
  local code=$?
  rm -rf "$CONFIG_DIR"
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -- "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 0.5
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    fi
  done
  wait "${pids[@]:-}" 2>/dev/null || true
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

wait_health() {
  local url="$1"
  local name="$2"
  local deadline_ns
  deadline_ns="$(
    python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" <<PY
import sys
import time

timeout = float(sys.argv[1])
print(time.monotonic_ns() + int(timeout * 1_000_000_000))
PY
  )"
  while true; do
    local probe_timeout
    probe_timeout="$(
      python3 - "$deadline_ns" "$HTTP_PROBE_TIMEOUT_SECONDS" <<PY
import sys
import time

deadline_ns = int(sys.argv[1])
probe_timeout = float(sys.argv[2])
remaining = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
if remaining <= 0:
    print("0")
else:
    print(f"{min(probe_timeout, remaining):.3f}")
PY
    )"
    if [[ "$probe_timeout" == "0" ]]; then
      break
    fi
    if curl --max-time "$probe_timeout" \
      --connect-timeout "$probe_timeout" \
      -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    python3 - "$deadline_ns" "$LOCAL_READINESS_POLL_SECONDS" <<PY
import sys
import time

deadline_ns = int(sys.argv[1])
poll = float(sys.argv[2])
remaining = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
if remaining > 0:
    time.sleep(min(poll, remaining))
PY
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $url" >&2
  return 1
}

start_server() {
  local name="$1"
  local project="$2"
  shift
  shift
  local config="$CONFIG_DIR/$name.json"
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- "$@"
  setsid dotnet run --no-build --project "$project" -- --config "$config" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"
dotnet build "$PUBLISHER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$SUBSCRIBER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

# The run owns its Redis: a dedicated, throwaway container is the shared
# location store every server registers into (no registry process exists).
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the PubSub E2E (it provisions a dedicated Redis container)." >&2
  exit 1
fi
zlink_redis_start_scoped_assign \
  REDIS_CONTAINER \
  REDIS_ENDPOINT \
  "zlink-redis-dotnet-e2e-pubsub" \
  "redis:7.2-alpine" \
  "$LOG_DIR"
zlink_redis_wait_ready "$REDIS_CONTAINER" "$REDIS_READINESS_TIMEOUT_SECONDS"
REDIS_KEY_PREFIX="pubsub-e2e:$RUN_ID:"

start_server pub-a "$PUBLISHER_PROJECT" \
  --rid pub-a \
  --http-url "$PUB_URL" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
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
  start_server "sub-$sub" "$SUBSCRIBER_PROJECT" \
    --rid "sub-$sub" \
    --http-url "${!url_var}" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --evidence-file "$LOG_DIR/sub-$sub.evidence.log" \
    --log-dir "$LOG_DIR" \
    "${extra_args[@]}"
  wait_health "${!url_var}" "sub-$sub"
done

sleep "$ROUTE_SETTLE_SECONDS"

python3 "$ROOT_DIR/../write_role_config.py" "$CONFIG_DIR/client.json" -- \
    --config-dir "$CONFIG_DIR" \
  --publisher-url "$PUB_URL" \
  --subscriber-url "$SUB_1_URL" \
  --subscriber-url "$SUB_2_URL" \
  --subscriber-url "$SUB_3_URL" \
  --late-subscriber-url "$SUB_LATE_URL" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --publisher-endpoint "$PUB_ENDPOINT" \
  --publisher-project "$PUBLISHER_PROJECT" \
  --subscriber-project "$SUBSCRIBER_PROJECT" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO"
dotnet run --no-build --project "$CLIENT_PROJECT" -- --config "$CONFIG_DIR/client.json" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
