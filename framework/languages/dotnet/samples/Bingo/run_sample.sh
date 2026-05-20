#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
mkdir -p "${LOG_DIR}"

PIDS=()

cleanup() {
  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  if [[ "${BINGO_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

BASE_PORT="${BINGO_BASE_PORT:-$(shuf -i 48000-52000 -n 1)}"
export BINGO_REGISTRY_PUB_ENDPOINT="${BINGO_REGISTRY_PUB_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 1))}"
export BINGO_REGISTRY_ROUTER_ENDPOINT="${BINGO_REGISTRY_ROUTER_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 2))}"
export BINGO_API_CHANNEL_ENDPOINT="${BINGO_API_CHANNEL_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 3))}"
export BINGO_PLAY_CHANNEL_ENDPOINT="${BINGO_PLAY_CHANNEL_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 4))}"
export BINGO_SESSION_ROUTER_ENDPOINT="${BINGO_SESSION_ROUTER_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 5))}"
export BINGO_RECONNECT_SESSION_ROUTER_ENDPOINT="${BINGO_RECONNECT_SESSION_ROUTER_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 6))}"
export BINGO_PLAY_ROUTER_ENDPOINT="${BINGO_PLAY_ROUTER_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 7))}"
export BINGO_PLAY_SPOT_ENDPOINT="${BINGO_PLAY_SPOT_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 8))}"
export BINGO_STREAM_ENDPOINT="${BINGO_STREAM_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 9))}"
export BINGO_RECONNECT_STREAM_ENDPOINT="${BINGO_RECONNECT_STREAM_ENDPOINT:-tcp://127.0.0.1:$((BASE_PORT + 10))}"
export BINGO_METADATA_DIR="${BINGO_METADATA_DIR:-${RUN_DIR}/metadata}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

start_server() {
  local name="$1"
  local project="$2"
  dotnet run --no-build --project "${project}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

dotnet build "${SCRIPT_DIR}/Bingo.csproj" --maxcpucount:1

start_server registry "${SCRIPT_DIR}/Server/Registry/Bingo.Server.Registry.csproj"
wait_port registry-router "${BINGO_REGISTRY_ROUTER_ENDPOINT}"

start_server api "${SCRIPT_DIR}/Server/Api/Bingo.Server.Api.csproj"
wait_port api "${BINGO_API_CHANNEL_ENDPOINT}"

start_server play "${SCRIPT_DIR}/Server/Play/Bingo.Server.Play.csproj"
wait_port play "${BINGO_PLAY_CHANNEL_ENDPOINT}"

start_server session "${SCRIPT_DIR}/Server/Session/Bingo.Server.Session.csproj"
wait_port session-stream "${BINGO_STREAM_ENDPOINT}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/Bingo.Client.csproj" -- \
  --stream-endpoint "${BINGO_STREAM_ENDPOINT}"
