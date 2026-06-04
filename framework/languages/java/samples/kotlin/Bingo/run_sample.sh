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
  local deadline=$((SECONDS + 15))
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
    for _ in range(9):
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

gradle_run() {
  gradle --no-daemon "$@" --quiet
}

read -r registry_pub_port registry_router_port api_port play_channel_port session_spot_port session_router_port play_spot_port play_router_port stream_port < <(reserve_ports)
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Dzlink.samples.bingo.registryPubEndpoint=tcp://127.0.0.1:${registry_pub_port} -Dzlink.samples.bingo.registryRouterEndpoint=tcp://127.0.0.1:${registry_router_port} -Dzlink.samples.bingo.apiChannelEndpoint=tcp://127.0.0.1:${api_port} -Dzlink.samples.bingo.playChannelEndpoint=tcp://127.0.0.1:${play_channel_port} -Dzlink.samples.bingo.sessionSpotEndpoint=tcp://127.0.0.1:${session_spot_port} -Dzlink.samples.bingo.sessionRouterEndpoint=tcp://127.0.0.1:${session_router_port} -Dzlink.samples.bingo.playSpotEndpoint=tcp://127.0.0.1:${play_spot_port} -Dzlink.samples.bingo.playSpotRouterEndpoint=tcp://127.0.0.1:${play_router_port} -Dzlink.samples.bingo.streamEndpoint=tcp://127.0.0.1:${stream_port}"

gradle_run :Server:Registry:run &
pids+=("$!")
wait_port "${registry_router_port}"
gradle_run :Server:Api:run &
pids+=("$!")
gradle_run :Server:Play:run &
pids+=("$!")
gradle_run :Server:Session:run &
pids+=("$!")
wait_port "${stream_port}"

gradle_run :Client:run
