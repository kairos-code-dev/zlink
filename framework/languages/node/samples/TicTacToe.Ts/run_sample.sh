#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
mkdir -p "${LOG_DIR}"

PIDS=()
REDIS_CONTAINER_ID=""

cleanup() {
  local status="$?"
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
  if [[ "${TICTACTOE_TS_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
  return "$status"
}
trap cleanup EXIT

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 15:
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

API_A_HTTP_ENDPOINT="${TICTACTOE_API_A_HTTP_ENDPOINT:-http://127.0.0.1:${PORTS[0]}}"
API_B_HTTP_ENDPOINT="${TICTACTOE_API_B_HTTP_ENDPOINT:-http://127.0.0.1:${PORTS[1]}}"
API_A_ENDPOINT="${TICTACTOE_API_A_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
API_B_ENDPOINT="${TICTACTOE_API_B_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
PLAY_A_CHANNEL_ENDPOINT="${TICTACTOE_PLAY_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
PLAY_B_CHANNEL_ENDPOINT="${TICTACTOE_PLAY_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
PLAY_A_STREAM_ENDPOINT="${TICTACTOE_PLAY_A_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
PLAY_B_STREAM_ENDPOINT="${TICTACTOE_PLAY_B_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
PLAY_A_SPOT_ENDPOINT="${TICTACTOE_PLAY_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
PLAY_B_SPOT_ENDPOINT="${TICTACTOE_PLAY_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
PLAY_A_ROUTE_ENDPOINT="${TICTACTOE_PLAY_A_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
PLAY_B_ROUTE_ENDPOINT="${TICTACTOE_PLAY_B_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
PLAY_A_SPOT_PUBSUB_ENDPOINT="${TICTACTOE_PLAY_A_SPOT_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"
PLAY_B_SPOT_PUBSUB_ENDPOINT="${TICTACTOE_PLAY_B_SPOT_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[13]}}"
REDIS_ENDPOINT="${TICTACTOE_REDIS_ENDPOINT:-127.0.0.1:${PORTS[14]}}"

API_A_CONFIG="${RUN_DIR}/sample.api-a.json"
API_B_CONFIG="${RUN_DIR}/sample.api-b.json"
PLAY_A_CONFIG="${RUN_DIR}/sample.play-a.json"
PLAY_B_CONFIG="${RUN_DIR}/sample.play-b.json"

if [[ -z "${TICTACTOE_REDIS_ENDPOINT:-}" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "TICTACTOE_REDIS_ENDPOINT is not set and docker is not available." >&2
    exit 1
  fi
  REDIS_CONTAINER_ID="$(docker run -d --rm -p "127.0.0.1:${PORTS[14]}:6379" redis:7-alpine)"
fi

python3 - "${API_A_CONFIG}" "${API_B_CONFIG}" "${PLAY_A_CONFIG}" "${PLAY_B_CONFIG}" <<PY
import json
import sys

api_a, api_b, play_a, play_b = sys.argv[1:]

def sample(instance, api_index, play_index, peer_play_index):
    return {
        "sample": {
            "instanceName": instance,
            "apiIndex": api_index,
            "playIndex": play_index,
            "apiHttpEndpoint": ["${API_A_HTTP_ENDPOINT}", "${API_B_HTTP_ENDPOINT}"][api_index],
            "apiEndpoints": ["${API_A_ENDPOINT}", "${API_B_ENDPOINT}"],
            "apiHttpEndpoints": ["${API_A_HTTP_ENDPOINT}", "${API_B_HTTP_ENDPOINT}"],
            "playEndpoint": ["${PLAY_A_CHANNEL_ENDPOINT}", "${PLAY_B_CHANNEL_ENDPOINT}"][play_index],
            "playChannelEndpoints": ["${PLAY_A_CHANNEL_ENDPOINT}", "${PLAY_B_CHANNEL_ENDPOINT}"],
            "playEndpoints": ["${PLAY_A_STREAM_ENDPOINT}", "${PLAY_B_STREAM_ENDPOINT}"],
            "playSpotEndpoint": ["${PLAY_A_SPOT_ENDPOINT}", "${PLAY_B_SPOT_ENDPOINT}"][play_index],
            "playSpotEndpoints": ["${PLAY_A_SPOT_ENDPOINT}", "${PLAY_B_SPOT_ENDPOINT}"],
            "playRouteEndpoint": ["${PLAY_A_ROUTE_ENDPOINT}", "${PLAY_B_ROUTE_ENDPOINT}"][play_index],
            "playRouteEndpoints": ["${PLAY_A_ROUTE_ENDPOINT}", "${PLAY_B_ROUTE_ENDPOINT}"],
            "playSpotPubSubEndpoint": ["${PLAY_A_SPOT_PUBSUB_ENDPOINT}", "${PLAY_B_SPOT_PUBSUB_ENDPOINT}"][play_index],
            "playSpotPubSubEndpoints": ["${PLAY_A_SPOT_PUBSUB_ENDPOINT}", "${PLAY_B_SPOT_PUBSUB_ENDPOINT}"],
            "playStreamEndpoint": ["${PLAY_A_STREAM_ENDPOINT}", "${PLAY_B_STREAM_ENDPOINT}"][play_index],
            "redisEndpoint": "${REDIS_ENDPOINT}",
            "playSpotNodeRid": f"play-node-{play_index + 1}",
            "peerPlaySpotNodeRid": f"play-node-{peer_play_index + 1}",
            "peerPlaySpotEndpoint": ["${PLAY_A_SPOT_ENDPOINT}", "${PLAY_B_SPOT_ENDPOINT}"][peer_play_index],
            "peerPlayRouteEndpoint": ["${PLAY_A_ROUTE_ENDPOINT}", "${PLAY_B_ROUTE_ENDPOINT}"][peer_play_index],
            "peerPlaySpotPubEndpoint": ["${PLAY_A_SPOT_PUBSUB_ENDPOINT}", "${PLAY_B_SPOT_PUBSUB_ENDPOINT}"][peer_play_index]
        }
    }

for path, payload in [
    (api_a, sample("api-a", 0, 0, 1)),
    (api_b, sample("api-b", 1, 0, 1)),
    (play_a, sample("play-a", 0, 0, 1)),
    (play_b, sample("play-b", 0, 1, 0)),
]:
    with open(path, "w", encoding="utf-8") as output:
        json.dump(payload, output, indent=2)
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

wait_ready_field() {
  local name="$1"
  local log_file="$2"
  local field="$3"
  local expected="$4"
  python3 - "${name}" "${log_file}" "${field}" "${expected}" <<'PY'
import json
import select
import sys
import time

name, log_file, field, expected = sys.argv[1:]
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    try:
        with open(log_file, "r", encoding="utf-8") as source:
            for line in source:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("event") == "ready" and event.get(field) == expected:
                    raise SystemExit(0)
    except FileNotFoundError:
        pass
    select.select([], [], [], 0.1)
print(f"Timed out waiting for {name} ready {field}={expected}", file=sys.stderr)
raise SystemExit(1)
PY
}

wait_event() {
  local name="$1"
  local log_file="$2"
  local event_name="$3"
  python3 - "${name}" "${log_file}" "${event_name}" <<'PY'
import json
import select
import sys
import time

name, log_file, event_name = sys.argv[1:]
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    try:
        with open(log_file, "r", encoding="utf-8") as source:
            for line in source:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("event") == event_name:
                    raise SystemExit(0)
    except FileNotFoundError:
        pass
    select.select([], [], [], 0.1)
print(f"Timed out waiting for {name} event={event_name}", file=sys.stderr)
raise SystemExit(1)
PY
}

wait_grep() {
  local name="$1"
  local pattern="$2"
  shift 2
  local deadline
  deadline=$((SECONDS + 10))
  while (( SECONDS < deadline )); do
    if grep -q "${pattern}" "$@" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name}" >&2
  return 1
}

start_server() {
  local name="$1"
  local entry="$2"
  local config="$3"
  ZLINK_SAMPLE_CONFIG="${config}" node "${SCRIPT_DIR}/${entry}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

(cd "${SCRIPT_DIR}" && npm run build >/dev/null)

wait_port redis "tcp://${REDIS_ENDPOINT}"

start_server play-b dist/Server/Play/main.js "${PLAY_B_CONFIG}"
wait_ready_field play-b "${LOG_DIR}/play-b.log" spotEndpoint "${PLAY_B_SPOT_ENDPOINT}"
wait_port play-b-channel "${PLAY_B_CHANNEL_ENDPOINT}"
wait_port play-b-stream "${PLAY_B_STREAM_ENDPOINT}"
wait_port play-b-spot-pubsub "${PLAY_B_SPOT_PUBSUB_ENDPOINT}"

start_server play-a dist/Server/Play/main.js "${PLAY_A_CONFIG}"
wait_ready_field play-a "${LOG_DIR}/play-a.log" spotEndpoint "${PLAY_A_SPOT_ENDPOINT}"
wait_port play-a-channel "${PLAY_A_CHANNEL_ENDPOINT}"
wait_port play-a-stream "${PLAY_A_STREAM_ENDPOINT}"
wait_port play-a-spot-pubsub "${PLAY_A_SPOT_PUBSUB_ENDPOINT}"
wait_event play-a "${LOG_DIR}/play-a.log" spotPeerReady
wait_event play-b "${LOG_DIR}/play-b.log" spotPeerReady

start_server api-a dist/Server/Api/main.js "${API_A_CONFIG}"
wait_port api-a-channel "${API_A_ENDPOINT}"
wait_port api-a-http "${API_A_HTTP_ENDPOINT}"

start_server api-b dist/Server/Api/main.js "${API_B_CONFIG}"
wait_port api-b-channel "${API_B_ENDPOINT}"
wait_port api-b-http "${API_B_HTTP_ENDPOINT}"

ZLINK_SAMPLE_CONFIG="${API_A_CONFIG}" node "${SCRIPT_DIR}/dist/Client/main.js" >"${LOG_DIR}/client.log" 2>&1
grep -q "stream-inbound sample=TicTacToe" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* name=.*Notify" "${LOG_DIR}/client.log"
grep -q "observer-win-milestone=verified" "${LOG_DIR}/client.log"
wait_grep "host leave marker" "actor: LeaveGameReq completed. actor=player-x" "${LOG_DIR}"/play-*.log
wait_grep "guest leave marker" "actor: LeaveGameReq completed. actor=player-o" "${LOG_DIR}"/play-*.log
echo "PASS TicTacToe.Ts"
