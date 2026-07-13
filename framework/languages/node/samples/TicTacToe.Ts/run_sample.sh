#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
export TICTACTOE_LOG_DIR="${RUN_DIR}/flow-logs"
mkdir -p "${LOG_DIR}" "${TICTACTOE_LOG_DIR}"

PIDS=()
REDIS_CONTAINER_ID=""
source "${SCRIPT_DIR}/../../e2e/redis-container.sh"
REDIS_KEY_PREFIX="zlink:sample:tictactoe:${RANDOM}:$$:"

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for file in "${LOG_DIR}"/*.log "${TICTACTOE_LOG_DIR}"/*.log; do
    [[ -f "${file}" ]] || continue
    echo "===== ${file} =====" >&2
    cat "${file}" >&2
  done
}

cleanup() {
  local status="$?"
  set +u
  print_logs "${status}"
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
    timeout -k 2s 10s docker rm -fv "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
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
    while len(sockets) < 12:
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
PLAY_A_STREAM_ENDPOINT="${TICTACTOE_PLAY_A_STREAM_ENDPOINT:-ws://127.0.0.1:${PORTS[6]}}"
PLAY_B_STREAM_ENDPOINT="${TICTACTOE_PLAY_B_STREAM_ENDPOINT:-ws://127.0.0.1:${PORTS[7]}}"
PLAY_A_SPOT_ENDPOINT="${TICTACTOE_PLAY_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
PLAY_B_SPOT_ENDPOINT="${TICTACTOE_PLAY_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
PLAY_A_SPOT_PUBSUB_ENDPOINT="${TICTACTOE_PLAY_A_SPOT_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
PLAY_B_SPOT_PUBSUB_ENDPOINT="${TICTACTOE_PLAY_B_SPOT_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
export TICTACTOE_API_A_HTTP_ENDPOINT="${API_A_HTTP_ENDPOINT}"
API_A_CONFIG="${RUN_DIR}/sample.api-a.json"
API_B_CONFIG="${RUN_DIR}/sample.api-b.json"
PLAY_A_CONFIG="${RUN_DIR}/sample.play-a.json"
PLAY_B_CONFIG="${RUN_DIR}/sample.play-b.json"

# The sample owns its Redis: always provision a dedicated, throwaway container
# so room-route state stays isolated per run and never touches a developer's
# local Redis. (REDIS_ENDPOINT is intentionally derived here, not read from env.)
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the TicTacToe sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
start_redis_container "zlink-redis-node-sample-${RANDOM}-$$" \
  --label "systems.zlink.sample=tictactoe-ts" \
  -p "127.0.0.1::6379" \
  "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
REDIS_ENDPOINT="$(redis_container_endpoint "${REDIS_CONTAINER_ID}")"

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
            "playSpotPubSubEndpoint": ["${PLAY_A_SPOT_PUBSUB_ENDPOINT}", "${PLAY_B_SPOT_PUBSUB_ENDPOINT}"][play_index],
            "playSpotPubSubEndpoints": ["${PLAY_A_SPOT_PUBSUB_ENDPOINT}", "${PLAY_B_SPOT_PUBSUB_ENDPOINT}"],
            "playStreamEndpoint": ["${PLAY_A_STREAM_ENDPOINT}", "${PLAY_B_STREAM_ENDPOINT}"][play_index],
            "redisEndpoint": "${REDIS_ENDPOINT}",
            "redisKeyPrefix": "${REDIS_KEY_PREFIX}",
            "playSpotNodeRid": f"play-node-{play_index + 1}",
            "peerPlaySpotNodeRid": f"play-node-{peer_play_index + 1}",
            "peerPlaySpotEndpoint": ["${PLAY_A_SPOT_ENDPOINT}", "${PLAY_B_SPOT_ENDPOINT}"][peer_play_index],
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
  endpoint="${endpoint#ws://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#ws://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local pid="${3:-}"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 600); do
    if [[ -n "${pid}" ]] && ! kill -0 "${pid}" 2>/dev/null; then
      echo "${name} process exited before ${endpoint} accepted connections" >&2
      return 1
    fi
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
  local pid="${5:-}"
  python3 - "${name}" "${log_file}" "${field}" "${expected}" "${pid}" <<'PY'
import json
import os
import select
import signal
import sys
import time

name, log_file, field, expected, pid_text = sys.argv[1:]
pid = int(pid_text) if pid_text else None
deadline = time.monotonic() + 60
while time.monotonic() < deadline:
    if pid is not None:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            print(f"{name} exited before ready {field}={expected}", file=sys.stderr)
            raise SystemExit(1)
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
deadline = time.monotonic() + 30
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
  deadline=$((SECONDS + 30))
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
  LAST_STARTED_PID="$!"
  PIDS+=("${LAST_STARTED_PID}")
}

(cd "${SCRIPT_DIR}" && npm run build >/dev/null)

wait_port redis "tcp://${REDIS_ENDPOINT}"

start_server play-b dist/Server/Play/main.js "${PLAY_B_CONFIG}"
wait_ready_field play-b "${LOG_DIR}/play-b.log" spotEndpoint "${PLAY_B_SPOT_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port play-b-channel "${PLAY_B_CHANNEL_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port play-b-stream "${PLAY_B_STREAM_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port play-b-spot-pubsub "${PLAY_B_SPOT_PUBSUB_ENDPOINT}" "${LAST_STARTED_PID}"

start_server play-a dist/Server/Play/main.js "${PLAY_A_CONFIG}"
wait_ready_field play-a "${LOG_DIR}/play-a.log" spotEndpoint "${PLAY_A_SPOT_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port play-a-channel "${PLAY_A_CHANNEL_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port play-a-stream "${PLAY_A_STREAM_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port play-a-spot-pubsub "${PLAY_A_SPOT_PUBSUB_ENDPOINT}" "${LAST_STARTED_PID}"
wait_event play-a "${LOG_DIR}/play-a.log" spotPeerReady
wait_event play-b "${LOG_DIR}/play-b.log" spotPeerReady

start_server api-a dist/Server/Api/main.js "${API_A_CONFIG}"
wait_ready_field api-a "${LOG_DIR}/api-a.log" endpoint "${API_A_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port api-a-channel "${API_A_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port api-a-http "${API_A_HTTP_ENDPOINT}" "${LAST_STARTED_PID}"

start_server api-b dist/Server/Api/main.js "${API_B_CONFIG}"
wait_ready_field api-b "${LOG_DIR}/api-b.log" endpoint "${API_B_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port api-b-channel "${API_B_ENDPOINT}" "${LAST_STARTED_PID}"
wait_port api-b-http "${API_B_HTTP_ENDPOINT}" "${LAST_STARTED_PID}"

node "${SCRIPT_DIR}/../../scripts/browser-e2e/run-sample.mjs" TicTacToe.Ts >"${LOG_DIR}/client.log" 2>&1
grep -q "stream-inbound sample=TicTacToe" "${LOG_DIR}/client.log"
grep -q "stream-inbound sample=TicTacToe client=host" "${LOG_DIR}/client.log"
grep -q "stream-inbound sample=TicTacToe client=guest" "${LOG_DIR}/client.log"
grep -q "stream-inbound sample=TicTacToe client=observer" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* name=.*Notify" "${LOG_DIR}/client.log"
grep -q "observer-win-milestone=verified" "${LOG_DIR}/client.log"
wait_grep "host leave marker" "actor: LeaveGameReq completed. actor=player-x" "${LOG_DIR}"/play-*.log
wait_grep "guest leave marker" "actor: LeaveGameReq completed. actor=player-o" "${LOG_DIR}"/play-*.log
wait_grep "host actor destroy marker" "entry spot: actor destroyed. actor=player-x" "${LOG_DIR}"/play-*.log
wait_grep "guest actor destroy marker" "entry spot: actor destroyed. actor=player-o" "${LOG_DIR}"/play-*.log
if grep -Rq "message flow phase=error" "${TICTACTOE_LOG_DIR}"; then
  echo "TicTacToe framework dispatch reported an error." >&2
  exit 1
fi
grep -Rq "message flow" "${TICTACTOE_LOG_DIR}"
echo "PASS TicTacToe.Ts"
