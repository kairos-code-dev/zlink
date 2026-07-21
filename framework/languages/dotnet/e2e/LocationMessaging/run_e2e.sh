#!/usr/bin/env bash
set -euo pipefail
umask 077

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
CONFIG_DIR="$(mktemp -d)"
if [[ "$#" -eq 0 ]]; then
  SCENARIO="all"
else
  SCENARIO="$*"
  SCENARIO="${SCENARIO// /,}"
fi
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
HTTP_PROBE_TIMEOUT_SECONDS=3
REDIS_READINESS_TIMEOUT_SECONDS=60

PROVIDER_PROJECT="$ROOT_DIR/Server/Provider/LocationMessaging.Provider.csproj"
WORKFLOW_PROJECT="$ROOT_DIR/Server/Workflow/LocationMessaging.Workflow.csproj"
CONSUMER_PROJECT="$ROOT_DIR/Server/Consumer/LocationMessaging.Consumer.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/LocationMessaging.Client.csproj"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

wait_tcp() {
  local host="$1"
  local port="$2"
  local name="$3"
  if python3 - "$host" "$port" "$REDIS_READINESS_TIMEOUT_SECONDS" <<'PY'
import socket
import sys
import time

host, port, timeout = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
deadline = time.monotonic() + timeout
while time.monotonic() < deadline:
    try:
        with socket.create_connection((host, port), timeout=1):
            sys.exit(0)
    except OSError:
        time.sleep(0.2)
sys.exit(1)
PY
  then
    return 0
  fi
  echo "Timed out waiting ${REDIS_READINESS_TIMEOUT_SECONDS}s for $name at $host:$port" >&2
  return 1
}

REDIS_CONTAINER=""
REDIS_HOST=""
REDIS_TCP_PORT=""
cleanup_redis() {
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  elif [[ -n "$REDIS_HOST" ]] && command -v redis-cli >/dev/null 2>&1; then
    # External instance: delete only this run's keys under its unique prefix.
    redis-cli -h "$REDIS_HOST" -p "$REDIS_TCP_PORT" --scan --pattern "$REDIS_KEY_PREFIX*" 2>/dev/null \
      | xargs -r redis-cli -h "$REDIS_HOST" -p "$REDIS_TCP_PORT" DEL >/dev/null 2>&1 || true
  fi
}

pids=()
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
  cleanup_redis
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

# --- Redis provisioning (doc §3 실행 모델) ---------------------------------
# Honor an externally provided instance; otherwise start a disposable
# container. Every run is isolated by a run-unique key prefix.
REDIS_KEY_PREFIX="zlink:e2e:cfg1:$(date +%s)-$$"
zlink_redis_start_scoped_assign \
  REDIS_CONTAINER \
  REDIS_ENDPOINT \
  "zlink-redis-dotnet-e2e-location-messaging" \
  "redis:7.2-alpine" \
  "$LOG_DIR"
echo "redis endpoint=$REDIS_ENDPOINT (container $REDIS_CONTAINER)"
REDIS_HOST="${REDIS_ENDPOINT%:*}"
REDIS_TCP_PORT="${REDIS_ENDPOINT##*:}"
if [[ -n "$REDIS_CONTAINER" ]]; then
  zlink_redis_wait_ready "$REDIS_CONTAINER" "$REDIS_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS"
fi
wait_tcp "$REDIS_HOST" "$REDIS_TCP_PORT" redis
echo "redis key prefix=$REDIS_KEY_PREFIX"

PROVIDER_A_HTTP_PORT="$(pick_port)"
PROVIDER_B_HTTP_PORT="$(pick_port)"
WORKFLOW_HTTP_PORT="$(pick_port)"
CONSUMER_HTTP_PORT="$(pick_port)"
SINGLE_CONSUMER_HTTP_PORT="$(pick_port)"
STORE_CONSUMER_HTTP_PORT="$(pick_port)"
BACKPRESSURE_CONSUMER_HTTP_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"
WORKFLOW_PORT="$(pick_port)"
ROUTE_A_PORT="$(pick_port)"
ROUTE_B_PORT="$(pick_port)"
CLIENT_ROUTE_PORT="$(pick_port)"

API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"
WORKFLOW="tcp://127.0.0.1:$WORKFLOW_PORT"
ROUTE_A="tcp://127.0.0.1:$ROUTE_A_PORT"
ROUTE_B="tcp://127.0.0.1:$ROUTE_B_PORT"
CLIENT_ROUTE="tcp://127.0.0.1:$CLIENT_ROUTE_PORT"

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

wait_route_ready() {
  local url="$1"
  local expected_count="$2"
  local name="$3"
  local deadline_ns
  deadline_ns="$(python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" <<'PY'
import sys
import time

print(time.monotonic_ns() + int(float(sys.argv[1]) * 1_000_000_000))
PY
  )"
  while true; do
    local probe_timeout
    probe_timeout="$(python3 - "$deadline_ns" "$HTTP_PROBE_TIMEOUT_SECONDS" <<'PY'
import sys
import time

remaining = (int(sys.argv[1]) - time.monotonic_ns()) / 1_000_000_000
print("0" if remaining <= 0 else f"{min(float(sys.argv[2]), remaining):.3f}")
PY
    )"
    if [[ "$probe_timeout" == "0" ]]; then
      break
    fi
    if curl --max-time "$probe_timeout" \
      --connect-timeout "$probe_timeout" \
      -fsS "$url/topology/ready?count=$expected_count" 2>/dev/null \
      | grep -Fq '"ready":true'; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name route readiness" >&2
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
dotnet build "$PROVIDER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$WORKFLOW_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CONSUMER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

start_server api-a "$PROVIDER_PROJECT" \
  --role provider \
  --rid api-a \
  --http-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --channel-endpoint "$API_A" \
  --max-message-size 2097152 \
  --route-endpoint "$ROUTE_A" \
  --route-peer "$ROUTE_B" \
  --weight 100 \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" api-a

start_server api-b "$PROVIDER_PROJECT" \
  --role provider \
  --rid api-b \
  --http-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --channel-endpoint "$API_B" \
  --max-message-size 2097152 \
  --route-endpoint "$ROUTE_B" \
  --route-peer "$ROUTE_A" \
  --weight 100 \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" api-b

start_server workflow-a "$WORKFLOW_PROJECT" \
  --role workflow \
  --rid workflow-a \
  --http-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --workflow-endpoint "$WORKFLOW" \
  --weight 100 \
  --evidence-file "$LOG_DIR/workflow-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$WORKFLOW_HTTP_PORT" workflow-a

start_server direct-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --provider-endpoint "$API_B" \
  --trace-label direct-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$CONSUMER_HTTP_PORT" direct-consumer

start_server single-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label single-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" single-consumer

start_server store-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$STORE_CONSUMER_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --trace-label store-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$STORE_CONSUMER_HTTP_PORT" store-consumer

start_server backpressure-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label backpressure-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" backpressure-consumer

wait_route_ready "http://127.0.0.1:$CONSUMER_HTTP_PORT" 2 direct-consumer
wait_route_ready "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" 1 single-consumer
wait_route_ready "http://127.0.0.1:$STORE_CONSUMER_HTTP_PORT" 2 store-consumer
wait_route_ready "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" 1 backpressure-consumer

set +e
python3 "$ROOT_DIR/../write_role_config.py" "$CONFIG_DIR/client.json" -- \
    --config-dir "$CONFIG_DIR" \
  --provider-a-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --provider-b-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --workflow-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --direct-consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --single-consumer-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --store-consumer-url "http://127.0.0.1:$STORE_CONSUMER_HTTP_PORT" \
  --backpressure-consumer-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --provider-project "$PROVIDER_PROJECT" \
  --consumer-project "$CONSUMER_PROJECT" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO"
dotnet run --no-build --project "$CLIENT_PROJECT" -- --config "$CONFIG_DIR/client.json" \
  > >(tee "$LOG_DIR/client.stdout.log") \
  2> >(tee "$LOG_DIR/client.stderr.log" >&2)
client_status=$?
set -e
exit "$client_status"
