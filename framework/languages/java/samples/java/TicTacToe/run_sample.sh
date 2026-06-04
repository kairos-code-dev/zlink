#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
cleanup() {
  for pid in "${pids[@]}"; do
    kill "$pid" >/dev/null 2>&1 || true
  done
}
trap cleanup EXIT

wait_port() {
  local port="$1"
  local deadline=$((SECONDS + 45))
  while (( SECONDS < deadline )); do
    if (echo >"/dev/tcp/127.0.0.1/$port") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for port $port" >&2
  return 1
}

reserve_ports() {
  python3 - <<'PY'
import socket
reserved = []
ports = []
try:
    for _ in range(6):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        reserved.append(sock)
        ports.append(str(sock.getsockname()[1]))
    print(" ".join(ports))
finally:
    for sock in reserved:
        sock.close()
PY
}

read -r api_port api_channel_port play_stream_port play_channel_port play_router_port spot_port < <(reserve_ports)

server_args=(
  "server"
  "--api-bind" "http://127.0.0.1:${api_port}"
  "--api-url" "http://127.0.0.1:${api_port}"
  "--api-channel-endpoint" "tcp://127.0.0.1:${api_channel_port}"
  "--play-channel-endpoint" "tcp://127.0.0.1:${play_channel_port}"
  "--play-router-endpoint" "tcp://127.0.0.1:${play_router_port}"
  "--play-endpoint" "tcp://127.0.0.1:${play_stream_port}"
  "--spot-endpoint" "tcp://127.0.0.1:${spot_port}"
)

gradle :Server:run --quiet --args="${server_args[*]}" &
pids+=("$!")
wait_port "${api_port}"
wait_port "${api_channel_port}"
wait_port "${play_stream_port}"
wait_port "${play_channel_port}"
wait_port "${api_port}"

gradle :Client:run --quiet --args="--api-url http://127.0.0.1:${api_port}"
