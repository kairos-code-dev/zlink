#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"
SCENARIO="${1:-${SCENARIO_SET:-DR-A1}}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
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

if [[ "$SCENARIO" == "all" ]]; then
  "$0" DR-A1
  "$0" DR-A2
  "$0" DR-A3
  "$0" DR-A4
  "$0" DR-B1
  "$0" DR-B2
  "$0" DR-B3
  "$0" DR-C1
  "$0" DR-C2
  "$0" DR-C3
  "$0" DR-D1
  "$0" DR-D2
  "$0" DR-D3
  "$0" DR-D4
  echo "discovery-registry-ha c++ e2e result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "a1" ]]; then
  SCENARIO="DR-A1"
elif [[ "$SCENARIO" == "dr-a2" ]]; then
  SCENARIO="DR-A2"
elif [[ "$SCENARIO" == "dr-a3" ]]; then
  SCENARIO="DR-A3"
elif [[ "$SCENARIO" == "dr-a4" ]]; then
  SCENARIO="DR-A4"
elif [[ "$SCENARIO" == "dr-b1" ]]; then
  SCENARIO="DR-B1"
elif [[ "$SCENARIO" == "dr-b2" ]]; then
  SCENARIO="DR-B2"
elif [[ "$SCENARIO" == "dr-b3" ]]; then
  SCENARIO="DR-B3"
elif [[ "$SCENARIO" == "dr-c1" ]]; then
  SCENARIO="DR-C1"
elif [[ "$SCENARIO" == "dr-c2" ]]; then
  SCENARIO="DR-C2"
elif [[ "$SCENARIO" == "dr-c3" ]]; then
  SCENARIO="DR-C3"
elif [[ "$SCENARIO" == "dr-d1" ]]; then
  SCENARIO="DR-D1"
elif [[ "$SCENARIO" == "dr-d2" ]]; then
  SCENARIO="DR-D2"
elif [[ "$SCENARIO" == "dr-d3" ]]; then
  SCENARIO="DR-D3"
elif [[ "$SCENARIO" == "dr-d4" ]]; then
  SCENARIO="DR-D4"
fi

if [[ "$SCENARIO" != "DR-A1" && "$SCENARIO" != "DR-A2" && "$SCENARIO" != "DR-A3" && "$SCENARIO" != "DR-A4" && "$SCENARIO" != "DR-B1" && "$SCENARIO" != "DR-B2" && "$SCENARIO" != "DR-B3" && "$SCENARIO" != "DR-C1" && "$SCENARIO" != "DR-C2" && "$SCENARIO" != "DR-C3" && "$SCENARIO" != "DR-D1" && "$SCENARIO" != "DR-D2" && "$SCENARIO" != "DR-D3" && "$SCENARIO" != "DR-D4" ]]; then
  echo "Unsupported C++ DiscoveryRegistryHa scenario: $SCENARIO" >&2
  exit 2
fi

read -r REG1_PUB REG1_ROUTER REG2_PUB REG2_ROUTER REG3_PUB REG3_ROUTER API_A API_B API_C EMBEDDED_PUB EMBEDDED_ROUTER EMBEDDED_API HTTP_REG1 HTTP_REG2 HTTP_REG3 HTTP_A HTTP_B HTTP_C HTTP_CONSUMER1 HTTP_CONSUMER2 HTTP_CONSUMER3 HTTP_EMBEDDED HTTP_EMBEDDED_CONSUMER HTTP_PROBE <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(24):
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
    ports.append(sock.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:12]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[12:]))
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
  zlink_cpp_e2e_discovery_registry_ha_registry \
  zlink_cpp_e2e_discovery_registry_ha_provider \
  zlink_cpp_e2e_discovery_registry_ha_consumer \
  zlink_cpp_e2e_discovery_registry_ha_embedded \
  zlink_cpp_e2e_discovery_registry_ha_probe \
  zlink_cpp_e2e_discovery_registry_ha_client >/dev/null

REGISTRY_SERVER="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_registry"
PROVIDER_SERVER="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_provider"
CONSUMER_SERVER="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_consumer"
EMBEDDED_SERVER="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_embedded"
PROBE_SERVER="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_probe"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_client"
PIDS=()

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
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
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $endpoint" >&2
  return 1
}

start_registry() {
  local rid="$1"
  local pub="$2"
  local router="$3"
  local http="$4"
  shift 4
  local env_args=(
    "ZLINK_CPP_DRHA_RID=$rid"
    "ZLINK_CPP_DRHA_REGISTRY_PUB=$pub"
    "ZLINK_CPP_DRHA_REGISTRY_ROUTER=$router"
    "ZLINK_CPP_DRHA_HTTP_ENDPOINT=$http"
    "ZLINK_CPP_DRHA_LOG_DIR=$LOG_DIR"
  )
  local peer_index=1
  for peer in "$@"; do
    env_args+=("ZLINK_CPP_DRHA_REGISTRY_PEER_${peer_index}=$peer")
    peer_index=$((peer_index + 1))
  done
  env "${env_args[@]}" "$REGISTRY_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  PIDS+=("$!")
  wait_port "$rid-router" "$router"
  wait_port "$rid-http" "$http"
}

start_provider() {
  local rid="$1"
  local channel="$2"
  local http="$3"
  local registry_router="$4"
  local log_name="${5:-$rid}"
  ZLINK_CPP_DRHA_RID="$rid" \
  ZLINK_CPP_DRHA_CHANNEL_ENDPOINT="$channel" \
  ZLINK_CPP_DRHA_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_DRHA_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
  ZLINK_CPP_DRHA_LOG_NAME="$log_name" \
    "$PROVIDER_SERVER" >"$LOG_DIR/$log_name.stdout.log" 2>"$LOG_DIR/$log_name.stderr.log" &
  PIDS+=("$!")
  wait_port "$log_name-channel" "$channel"
  wait_port "$log_name-http" "$http"
}

start_consumer() {
  local rid="$1"
  local registry_router="$2"
  local http="$3"
  shift 3
  local env_args=(
    "ZLINK_CPP_DRHA_RID=$rid"
    "ZLINK_CPP_DRHA_HTTP_ENDPOINT=$http"
    "ZLINK_CPP_DRHA_REGISTRY_ROUTER=$registry_router"
    "ZLINK_CPP_DRHA_LOG_DIR=$LOG_DIR"
  )
  local router_index=1
  for extra_router in "$@"; do
    env_args+=("ZLINK_CPP_DRHA_REGISTRY_ROUTER_${router_index}=$extra_router")
    router_index=$((router_index + 1))
  done
  env "${env_args[@]}" "$CONSUMER_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  PIDS+=("$!")
  wait_port "$rid-http" "$http"
}

start_embedded() {
  local rid="$1"
  local pub="$2"
  local router="$3"
  local channel="$4"
  local http="$5"
  shift 5
  local env_args=(
    "ZLINK_CPP_DRHA_RID=$rid"
    "ZLINK_CPP_DRHA_REGISTRY_PUB=$pub"
    "ZLINK_CPP_DRHA_REGISTRY_ROUTER=$router"
    "ZLINK_CPP_DRHA_CHANNEL_ENDPOINT=$channel"
    "ZLINK_CPP_DRHA_HTTP_ENDPOINT=$http"
    "ZLINK_CPP_DRHA_LOG_DIR=$LOG_DIR"
    "ZLINK_CPP_DRHA_LOG_NAME=$rid"
  )
  local peer_index=1
  for peer in "$@"; do
    env_args+=("ZLINK_CPP_DRHA_REGISTRY_PEER_${peer_index}=$peer")
    peer_index=$((peer_index + 1))
  done
  env "${env_args[@]}" "$EMBEDDED_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  PIDS+=("$!")
  wait_port "$rid-router" "$router"
  wait_port "$rid-channel" "$channel"
  wait_port "$rid-http" "$http"
}

start_probe() {
  local registry_router="$1"
  local http="$2"
  ZLINK_CPP_DRHA_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_DRHA_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$PROBE_SERVER" >"$LOG_DIR/probe.stdout.log" 2>"$LOG_DIR/probe.stderr.log" &
  PIDS+=("$!")
  wait_port "probe-http" "$http"
}

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  fi
}

if [[ "$SCENARIO" == "DR-A1" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG1_ROUTER"
  start_consumer consumer-a1 "$REG1_ROUTER" "$HTTP_CONSUMER1"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-A1" \
  ZLINK_CPP_DRHA_REGISTRY_URL="$HTTP_REG1" \
  ZLINK_CPP_DRHA_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-a1.stdout.log" 2>"$LOG_DIR/client-a1.stderr.log"
  cat "$LOG_DIR/client-a1.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-A1 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-A2" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER1"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-A2" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-a2.stdout.log" 2>"$LOG_DIR/client-dr-a2.stderr.log"
  cat "$LOG_DIR/client-dr-a2.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-A2 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-A4" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER" api-a-primary
  start_provider api-a "$API_B" "$HTTP_B" "$REG2_ROUTER" api-a-duplicate
  start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER2"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-A4" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_DUPLICATE_PROVIDER_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_DUPLICATE_API_A_ENDPOINT="$API_B" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-a4.stdout.log" 2>"$LOG_DIR/client-dr-a4.stderr.log"
  cat "$LOG_DIR/client-dr-a4.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-A4 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-B1" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  api_a_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_provider api-b "$API_B" "$HTTP_B" "$REG1_ROUTER"
  api_b_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_consumer consumer-a1 "$REG1_ROUTER" "$HTTP_CONSUMER1"

  sleep "$ROUTE_SETTLE_SECONDS"
  stop_pid "$api_a_pid"
  stop_pid "$api_b_pid"

  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
  start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER2"
  start_consumer consumer-reg3 "$REG3_ROUTER" "$HTTP_CONSUMER3"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-B1" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_REG3_URL="$HTTP_REG3" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_REG3_CONSUMER_URL="$HTTP_CONSUMER3" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_API_B_ENDPOINT="$API_B" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-b1.stdout.log" 2>"$LOG_DIR/client-dr-b1.stderr.log"
  cat "$LOG_DIR/client-dr-b1.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-B1 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-B2" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  reg2_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
  start_consumer consumer-reg1-reg2 "$REG1_ROUTER" "$HTTP_CONSUMER1" "$REG2_ROUTER"

  sleep "$ROUTE_SETTLE_SECONDS"
  stop_pid "$reg2_pid"
  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-B2" \
  ZLINK_CPP_DRHA_REG1_URL="$HTTP_REG1" \
  ZLINK_CPP_DRHA_REG1_REG2_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-b2.stdout.log" 2>"$LOG_DIR/client-dr-b2.stderr.log"
  cat "$LOG_DIR/client-dr-b2.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-B2 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-B3" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  reg2_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
  start_consumer consumer-reg1 "$REG1_ROUTER" "$HTTP_CONSUMER1"
  start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER2"

  sleep "$ROUTE_SETTLE_SECONDS"
  stop_pid "$reg2_pid"
  sleep "$ROUTE_SETTLE_SECONDS"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-B3" \
  ZLINK_CPP_DRHA_REG1_URL="$HTTP_REG1" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-b3.stdout.log" 2>"$LOG_DIR/client-dr-b3.stderr.log"
  cat "$LOG_DIR/client-dr-b3.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-B3 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-C1" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  reg2_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
  start_consumer consumer-reg1 "$REG1_ROUTER" "$HTTP_CONSUMER1"

  sleep "$ROUTE_SETTLE_SECONDS"
  stop_pid "$reg2_pid"
  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-C1" \
  ZLINK_CPP_DRHA_REG1_URL="$HTTP_REG1" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-c1.stdout.log" 2>"$LOG_DIR/client-dr-c1.stderr.log"
  cat "$LOG_DIR/client-dr-c1.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-C1 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-C2" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  reg2_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
  start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER2"

  sleep "$ROUTE_SETTLE_SECONDS"
  stop_pid "$reg2_pid"
  sleep "$ROUTE_SETTLE_SECONDS"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-C2" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-c2.stdout.log" 2>"$LOG_DIR/client-dr-c2.stderr.log"
  cat "$LOG_DIR/client-dr-c2.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-C2 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-C3" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  reg1_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  reg2_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  reg3_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  api_a_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
  api_b_pid="${PIDS[$((${#PIDS[@]} - 1))]}"
  start_consumer consumer-reg1 "$REG1_ROUTER" "$HTTP_CONSUMER1"
  start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER2"
  consumer_reg2_pid="${PIDS[$((${#PIDS[@]} - 1))]}"

  sleep "$ROUTE_SETTLE_SECONDS"
  ZLINK_CPP_DRHA_SCENARIO="DR-C3" \
  ZLINK_CPP_DRHA_C3_PHASE="before" \
  ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_PROVIDER_C_URL="$HTTP_C" \
  ZLINK_CPP_DRHA_PROVIDER_C_ENDPOINT="$API_C" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-c3-before.stdout.log" 2>"$LOG_DIR/client-dr-c3-before.stderr.log"
  cat "$LOG_DIR/client-dr-c3-before.stdout.log"

  stop_pid "$reg1_pid"
  stop_pid "$reg2_pid"
  stop_pid "$reg3_pid"
  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-C3" \
  ZLINK_CPP_DRHA_C3_PHASE="during" \
  ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_PROVIDER_C_URL="$HTTP_C" \
  ZLINK_CPP_DRHA_PROVIDER_C_ENDPOINT="$API_C" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-c3-during.stdout.log" 2>"$LOG_DIR/client-dr-c3-during.stderr.log"
  cat "$LOG_DIR/client-dr-c3-during.stdout.log"

  stop_pid "$api_a_pid"
  stop_pid "$api_b_pid"
  start_registry reg-1-after "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
  start_registry reg-2-after "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
  start_registry reg-3-after "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
  start_provider api-c "$API_C" "$HTTP_C" "$REG1_ROUTER"
  stop_pid "$consumer_reg2_pid"
  start_consumer consumer-reg2-recovered "$REG2_ROUTER" "$HTTP_CONSUMER2"

  sleep "$ROUTE_SETTLE_SECONDS"
  ZLINK_CPP_DRHA_SCENARIO="DR-C3" \
  ZLINK_CPP_DRHA_C3_PHASE="after" \
  ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_PROVIDER_C_URL="$HTTP_C" \
  ZLINK_CPP_DRHA_PROVIDER_C_ENDPOINT="$API_C" \
  ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-c3-after.stdout.log" 2>"$LOG_DIR/client-dr-c3-after.stderr.log"
  cat "$LOG_DIR/client-dr-c3-after.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-C3 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-D1" ]]; then
  start_embedded embedded-api "$EMBEDDED_PUB" "$EMBEDDED_ROUTER" "$EMBEDDED_API" "$HTTP_EMBEDDED"
  start_consumer embedded-consumer "$EMBEDDED_ROUTER" "$HTTP_EMBEDDED_CONSUMER"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-D1" \
  ZLINK_CPP_DRHA_EMBEDDED_URL="$HTTP_EMBEDDED" \
  ZLINK_CPP_DRHA_EMBEDDED_CONSUMER_URL="$HTTP_EMBEDDED_CONSUMER" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-d1.stdout.log" 2>"$LOG_DIR/client-dr-d1.stderr.log"
  cat "$LOG_DIR/client-dr-d1.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-D1 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-D2" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG1_ROUTER"
  start_consumer consumer-reg1 "$REG1_ROUTER" "$HTTP_CONSUMER1"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-D2" \
  ZLINK_CPP_DRHA_REG1_URL="$HTTP_REG1" \
  ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-d2.stdout.log" 2>"$LOG_DIR/client-dr-d2.stderr.log"
  cat "$LOG_DIR/client-dr-d2.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-D2 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-D3" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$EMBEDDED_PUB"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_provider api-b "$API_B" "$HTTP_B" "$REG1_ROUTER"
  start_embedded embedded-api-mixed "$EMBEDDED_PUB" "$EMBEDDED_ROUTER" "$EMBEDDED_API" "$HTTP_EMBEDDED" "$REG1_PUB"
  start_consumer embedded-consumer "$EMBEDDED_ROUTER" "$HTTP_EMBEDDED_CONSUMER"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-D3" \
  ZLINK_CPP_DRHA_EMBEDDED_URL="$HTTP_EMBEDDED" \
  ZLINK_CPP_DRHA_EMBEDDED_CONSUMER_URL="$HTTP_EMBEDDED_CONSUMER" \
  ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
  ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
  ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-d3.stdout.log" 2>"$LOG_DIR/client-dr-d3.stderr.log"
  cat "$LOG_DIR/client-dr-d3.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-D3 result=passed"
  exit 0
fi

if [[ "$SCENARIO" == "DR-D4" ]]; then
  start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1"
  start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
  start_probe "$REG1_ROUTER" "$HTTP_PROBE"

  sleep "$ROUTE_SETTLE_SECONDS"

  ZLINK_CPP_DRHA_SCENARIO="DR-D4" \
  ZLINK_CPP_DRHA_REG1_URL="$HTTP_REG1" \
  ZLINK_CPP_DRHA_PROBE_URL="$HTTP_PROBE" \
  ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-dr-d4.stdout.log" 2>"$LOG_DIR/client-dr-d4.stderr.log"
  cat "$LOG_DIR/client-dr-d4.stdout.log"

  echo "discovery-registry-ha c++ scenario=DR-D4 result=passed"
  exit 0
fi

start_registry reg-1 "$REG1_PUB" "$REG1_ROUTER" "$HTTP_REG1" "$REG2_PUB" "$REG3_PUB"
start_registry reg-2 "$REG2_PUB" "$REG2_ROUTER" "$HTTP_REG2" "$REG1_PUB" "$REG3_PUB"
start_registry reg-3 "$REG3_PUB" "$REG3_ROUTER" "$HTTP_REG3" "$REG1_PUB" "$REG2_PUB"
start_provider api-a "$API_A" "$HTTP_A" "$REG1_ROUTER"
start_provider api-b "$API_B" "$HTTP_B" "$REG3_ROUTER"
start_consumer consumer-reg1 "$REG1_ROUTER" "$HTTP_CONSUMER1"
start_consumer consumer-reg2 "$REG2_ROUTER" "$HTTP_CONSUMER2"
start_consumer consumer-reg3 "$REG3_ROUTER" "$HTTP_CONSUMER3"

sleep "$ROUTE_SETTLE_SECONDS"

ZLINK_CPP_DRHA_SCENARIO="DR-A3" \
ZLINK_CPP_DRHA_REG1_URL="$HTTP_REG1" \
ZLINK_CPP_DRHA_REG2_URL="$HTTP_REG2" \
ZLINK_CPP_DRHA_REG3_URL="$HTTP_REG3" \
ZLINK_CPP_DRHA_REG1_CONSUMER_URL="$HTTP_CONSUMER1" \
ZLINK_CPP_DRHA_REG2_CONSUMER_URL="$HTTP_CONSUMER2" \
ZLINK_CPP_DRHA_REG3_CONSUMER_URL="$HTTP_CONSUMER3" \
ZLINK_CPP_DRHA_PROVIDER_A_URL="$HTTP_A" \
ZLINK_CPP_DRHA_PROVIDER_B_URL="$HTTP_B" \
ZLINK_CPP_DRHA_API_A_ENDPOINT="$API_A" \
ZLINK_CPP_DRHA_API_B_ENDPOINT="$API_B" \
ZLINK_CPP_DRHA_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/client-dr-a3.stdout.log" 2>"$LOG_DIR/client-dr-a3.stderr.log"
cat "$LOG_DIR/client-dr-a3.stdout.log"

echo "discovery-registry-ha c++ scenario=DR-A3 result=passed"
