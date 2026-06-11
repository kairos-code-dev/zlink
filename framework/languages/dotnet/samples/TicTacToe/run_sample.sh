#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
mkdir -p "${LOG_DIR}"

PIDS=()

cleanup() {
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -INT "${pid}" 2>/dev/null || true
    fi
  done
  for _ in $(seq 1 20); do
    local any_alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "${pid}" 2>/dev/null; then
        any_alive=1
        break
      fi
    done
    if [[ "${any_alive}" == "0" ]]; then
      break
    fi
    sleep 0.1
  done
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  if [[ "${TICTACTOE_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${TICTACTOE_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 6); do
    PORTS+=("$((TICTACTOE_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 6:
        port = random.randint(48000, 60999)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"
fi

API_BIND_URL="${TICTACTOE_API_BIND_URL:-http://127.0.0.1:${PORTS[0]}}"
API_PUBLIC_URL="${TICTACTOE_API_PUBLIC_URL:-${API_BIND_URL}}"
API_CHANNEL_ENDPOINT="${TICTACTOE_API_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[1]}}"
PLAY_CHANNEL_ENDPOINT="${TICTACTOE_PLAY_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
PLAY_ROUTER_ENDPOINT="${TICTACTOE_PLAY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
PLAY_ENDPOINT="${TICTACTOE_PLAY_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
SPOT_ENDPOINT="${TICTACTOE_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
CONFIG_FILE="${RUN_DIR}/appsettings.json"

python3 - "${CONFIG_FILE}" <<PY
import json
import sys

path = sys.argv[1]
settings = {
    "Sample": {
        "ApiBindUrl": "${API_BIND_URL}",
        "ApiPublicUrl": "${API_PUBLIC_URL}",
        "ApiChannelEndpoint": "${API_CHANNEL_ENDPOINT}",
        "PlayChannelEndpoint": "${PLAY_CHANNEL_ENDPOINT}",
        "PlayRouterEndpoint": "${PLAY_ROUTER_ENDPOINT}",
        "PlayEndpoint": "${PLAY_ENDPOINT}",
        "SpotEndpoint": "${SPOT_ENDPOINT}",
        "LogDirectory": "${LOG_DIR}"
    }
}
with open(path, "w", encoding="utf-8") as output:
    json.dump(settings, output, indent=2)
PY

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
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
  local mode="$2"
  dotnet run --no-build --project "${SCRIPT_DIR}/Server/TicTacToe.Server.csproj" -- \
    "${mode}" --config "${CONFIG_FILE}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

dotnet build "${SCRIPT_DIR}/TicTacToe.sln" --maxcpucount:1

start_server play play
wait_port play-stream "${PLAY_ENDPOINT}"
wait_port play-channel "${PLAY_CHANNEL_ENDPOINT}"
wait_port play-router "${PLAY_ROUTER_ENDPOINT}"

start_server api api
wait_port api-http "${API_BIND_URL}"
wait_port api-channel "${API_CHANNEL_ENDPOINT}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/TicTacToe.Client.csproj" -- \
  --api-url "${API_PUBLIC_URL}"
