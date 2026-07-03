#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build-redis-vcpkg}"
SCENARIO="${1:-${SCENARIO_SET:-SF-A1}}"
HEARTBEAT_MS="${ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS:-1000}"
LEASE_TTL_MS="${ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS:-3000}"
POLLING_MS="${ZLINK_CPP_SF_LOCATION_POLLING_MS:-500}"
GRACE_MS="${ZLINK_CPP_SF_LOCATION_GRACE_MS:-6000}"

normalize_scenario() {
  case "$1" in
    all) echo all ;;
    sf-a1|SF-A1|a1) echo SF-A1 ;;
    sf-a2|SF-A2|a2) echo SF-A2 ;;
    sf-b1|SF-B1|b1) echo SF-B1 ;;
    sf-b2|SF-B2|b2) echo SF-B2 ;;
    sf-c1|SF-C1|c1) echo SF-C1 ;;
    sf-c2|SF-C2|c2) echo SF-C2 ;;
    sf-d1|SF-D1|d1) echo SF-D1 ;;
    sf-d2|SF-D2|d2) echo SF-D2 ;;
    sf-d3|SF-D3|d3) echo SF-D3 ;;
    *) echo "Unsupported C++ StoreFailure scenario: $1" >&2; exit 2 ;;
  esac
}

SCENARIO="$(normalize_scenario "$SCENARIO")"
if [[ "$SCENARIO" == "all" ]]; then
  "$0" SF-A1
  "$0" SF-A2
  "$0" SF-B1
  "$0" SF-B2
  "$0" SF-C1
  "$0" SF-C2
  "$0" SF-D1
  "$0" SF-D2
  "$0" SF-D3
  echo "store-failure c++ e2e result=passed"
  exit 0
fi

read -r API_A API_B HTTP_A HTTP_B HTTP_CONSUMER REDIS_PORT <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(6):
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
    ports.append(sock.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:2]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[2:5]), end=" ")
print(ports[5])
for sock in sockets:
    sock.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_store_failure_provider \
  zlink_cpp_e2e_store_failure_consumer \
  zlink_cpp_e2e_store_failure_client >/dev/null

PROVIDER_SERVER="$BUILD_DIR/zlink_cpp_e2e_store_failure_provider"
CONSUMER_SERVER="$BUILD_DIR/zlink_cpp_e2e_store_failure_consumer"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_store_failure_client"
REDIS_CONTAINER="zlink-cpp-store-failure-${RUN_ID}"
REDIS_ENDPOINT="127.0.0.1:${REDIS_PORT}"
REDIS_KEY_PREFIX="zlink:cpp:store-failure:${RUN_ID}"
PIDS=()

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
  exit "$code"
}
trap cleanup EXIT

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 120); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

docker run --rm -d \
  --name "$REDIS_CONTAINER" \
  -p "127.0.0.1:${REDIS_PORT}:6379" \
  redis:7-alpine >/dev/null

for _ in $(seq 1 80); do
  if docker exec "$REDIS_CONTAINER" redis-cli ping >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

start_provider() {
  local rid="$1"
  local channel="$2"
  local http="$3"
  ZLINK_CPP_DRHA_RID="$rid" \
  ZLINK_CPP_DRHA_CHANNEL_ENDPOINT="$channel" \
  ZLINK_CPP_DRHA_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS="$HEARTBEAT_MS" \
  ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS="$LEASE_TTL_MS" \
  ZLINK_CPP_SF_LOCATION_POLLING_MS="$POLLING_MS" \
  ZLINK_CPP_SF_LOCATION_GRACE_MS="$GRACE_MS" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
  ZLINK_CPP_DRHA_LOG_NAME="$rid" \
    "$PROVIDER_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  PIDS+=("$!")
  wait_port "$rid-channel" "$channel"
  wait_port "$rid-http" "$http"
}

start_consumer() {
  local rid="$1"
  local http="$2"
  ZLINK_CPP_DRHA_RID="$rid" \
  ZLINK_CPP_DRHA_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS="$HEARTBEAT_MS" \
  ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS="$LEASE_TTL_MS" \
  ZLINK_CPP_SF_LOCATION_POLLING_MS="$POLLING_MS" \
  ZLINK_CPP_SF_LOCATION_GRACE_MS="$GRACE_MS" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CONSUMER_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  PIDS+=("$!")
  wait_port "$rid-http" "$http"
}

start_provider api-a "$API_A" "$HTTP_A"
start_provider api-b "$API_B" "$HTTP_B"
start_consumer consumer "$HTTP_CONSUMER"

sleep 2

ZLINK_CPP_SF_SCENARIO="$SCENARIO" \
ZLINK_CPP_SF_CONSUMER_URL="$HTTP_CONSUMER" \
ZLINK_CPP_SF_PROVIDER_A_URL="$HTTP_A" \
ZLINK_CPP_SF_PROVIDER_B_URL="$HTTP_B" \
ZLINK_CPP_E2E_REDIS_CONTAINER="$REDIS_CONTAINER" \
ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS="$HEARTBEAT_MS" \
ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS="$LEASE_TTL_MS" \
ZLINK_CPP_SF_LOCATION_POLLING_MS="$POLLING_MS" \
ZLINK_CPP_SF_LOCATION_GRACE_MS="$GRACE_MS" \
  "$CLIENT" >"$LOG_DIR/client-$SCENARIO.stdout.log" 2>"$LOG_DIR/client-$SCENARIO.stderr.log"
cat "$LOG_DIR/client-$SCENARIO.stdout.log"

echo "store-failure c++ scenario=$SCENARIO result=passed"
