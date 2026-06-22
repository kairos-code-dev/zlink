#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
mkdir -p "${LOG_DIR}"

PIDS=()
REDIS_CONTAINER_ID=""

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
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    docker rm -f "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
  if [[ "${TICTACTOE_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${TICTACTOE_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 13); do
    PORTS+=("$((TICTACTOE_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 13:
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

API_A_BIND_URL="${TICTACTOE_API_A_BIND_URL:-http://127.0.0.1:${PORTS[0]}}"
API_B_BIND_URL="${TICTACTOE_API_B_BIND_URL:-http://127.0.0.1:${PORTS[1]}}"
API_A_PUBLIC_URL="${TICTACTOE_API_A_PUBLIC_URL:-${API_A_BIND_URL}}"
API_B_PUBLIC_URL="${TICTACTOE_API_B_PUBLIC_URL:-${API_B_BIND_URL}}"
API_A_CHANNEL_ENDPOINT="${TICTACTOE_API_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
API_B_CHANNEL_ENDPOINT="${TICTACTOE_API_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
PLAY_A_CHANNEL_ENDPOINT="${TICTACTOE_PLAY_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
PLAY_B_CHANNEL_ENDPOINT="${TICTACTOE_PLAY_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
PLAY_A_ENDPOINT="${TICTACTOE_PLAY_A_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
PLAY_B_ENDPOINT="${TICTACTOE_PLAY_B_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
SPOT_A_ENDPOINT="${TICTACTOE_SPOT_A_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
SPOT_B_ENDPOINT="${TICTACTOE_SPOT_B_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
SPOT_A_PUBSUB_ENDPOINT="${TICTACTOE_SPOT_A_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
SPOT_B_PUBSUB_ENDPOINT="${TICTACTOE_SPOT_B_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
API_A_CONFIG_FILE="${RUN_DIR}/appsettings.api-a.json"
API_B_CONFIG_FILE="${RUN_DIR}/appsettings.api-b.json"
PLAY_A_CONFIG_FILE="${RUN_DIR}/appsettings.play-a.json"
PLAY_B_CONFIG_FILE="${RUN_DIR}/appsettings.play-b.json"

# The sample owns its Redis: always provision a dedicated, throwaway container
# so room-route state stays isolated per run and never touches a developer's
# local Redis. (TICTACTOE_REDIS_ENDPOINT is intentionally derived here, not read.)
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the TicTacToe sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
REDIS_CONTAINER_ID="$(docker run -d --rm --name "zlink-tictactoe-dotnet-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7-alpine)"
REDIS_ENDPOINT="$(docker port "${REDIS_CONTAINER_ID}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"

python3 - "${API_A_CONFIG_FILE}" "${API_B_CONFIG_FILE}" "${PLAY_A_CONFIG_FILE}" "${PLAY_B_CONFIG_FILE}" <<PY
import json
import sys

api_a_path, api_b_path, play_a_path, play_b_path = sys.argv[1:]

def sample(instance_name, api_index, play_index, peer_play_index):
    return {
        "Sample": {
            "InstanceName": instance_name,
            "ApiIndex": api_index,
            "PlayIndex": play_index,
            "ApiBindUrls": ["${API_A_BIND_URL}", "${API_B_BIND_URL}"],
            "ApiPublicUrls": ["${API_A_PUBLIC_URL}", "${API_B_PUBLIC_URL}"],
            "ApiChannelEndpoints": ["${API_A_CHANNEL_ENDPOINT}", "${API_B_CHANNEL_ENDPOINT}"],
            "PlayChannelEndpoints": ["${PLAY_A_CHANNEL_ENDPOINT}", "${PLAY_B_CHANNEL_ENDPOINT}"],
            "PlayEndpoints": ["${PLAY_A_ENDPOINT}", "${PLAY_B_ENDPOINT}"],
            "SpotEndpoints": ["${SPOT_A_ENDPOINT}", "${SPOT_B_ENDPOINT}"],
            "SpotPubSubEndpoints": ["${SPOT_A_PUBSUB_ENDPOINT}", "${SPOT_B_PUBSUB_ENDPOINT}"],
            "PlaySpotNodeRid": f"play-node-{play_index + 1}",
            "PeerPlaySpotNodeRid": f"play-node-{peer_play_index + 1}",
            "PeerSpotEndpoint": ["${SPOT_A_ENDPOINT}", "${SPOT_B_ENDPOINT}"][peer_play_index],
            "PeerSpotPubEndpoint": ["${SPOT_A_PUBSUB_ENDPOINT}", "${SPOT_B_PUBSUB_ENDPOINT}"][peer_play_index],
            "RedisEndpoint": "${REDIS_ENDPOINT}",
            "LogDirectory": "${LOG_DIR}"
        }
    }

for path, settings in [
    (api_a_path, sample("api-a", 0, 0, 1)),
    (api_b_path, sample("api-b", 1, 0, 1)),
    (play_a_path, sample("play-a", 0, 0, 1)),
    (play_b_path, sample("play-b", 0, 1, 0)),
]:
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
  local config_file="$3"
  dotnet run --no-build --project "${SCRIPT_DIR}/Server/TicTacToe.Server.csproj" -- \
    "${mode}" --config "${config_file}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

dotnet build "${SCRIPT_DIR}/TicTacToe.sln" --maxcpucount:1

wait_port redis "tcp://${REDIS_ENDPOINT}"

start_server play-a play-a "${PLAY_A_CONFIG_FILE}"
wait_port play-a-stream "${PLAY_A_ENDPOINT}"
wait_port play-a-channel "${PLAY_A_CHANNEL_ENDPOINT}"
wait_port play-a-spot "${SPOT_A_ENDPOINT}"
wait_port play-a-spot-pubsub "${SPOT_A_PUBSUB_ENDPOINT}"

start_server play-b play-b "${PLAY_B_CONFIG_FILE}"
wait_port play-b-stream "${PLAY_B_ENDPOINT}"
wait_port play-b-channel "${PLAY_B_CHANNEL_ENDPOINT}"
wait_port play-b-spot "${SPOT_B_ENDPOINT}"
wait_port play-b-spot-pubsub "${SPOT_B_PUBSUB_ENDPOINT}"
sleep 2

start_server api-a api-a "${API_A_CONFIG_FILE}"
wait_port api-a-http "${API_A_BIND_URL}"
wait_port api-a-channel "${API_A_CHANNEL_ENDPOINT}"

start_server api-b api-b "${API_B_CONFIG_FILE}"
wait_port api-b-http "${API_B_BIND_URL}"
wait_port api-b-channel "${API_B_CHANNEL_ENDPOINT}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/TicTacToe.Client.csproj" -- \
  --api-url "${API_A_PUBLIC_URL}" >"${LOG_DIR}/client.log" 2>&1
grep -q "stream-inbound sample=TicTacToe" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* name=.*Notify" "${LOG_DIR}/client.log"
grep -q "observer-win-milestone=verified" "${LOG_DIR}/client.log"
grep -q "actor: LeaveGameReq completed. actor=player-x" "${LOG_DIR}"/play-*.log
grep -q "actor: LeaveGameReq completed. actor=player-o" "${LOG_DIR}"/play-*.log
grep -q "entry spot: actor destroy completed. actor=player-x" "${LOG_DIR}"/play-*.log
grep -q "entry spot: actor destroy completed. actor=player-o" "${LOG_DIR}"/play-*.log
