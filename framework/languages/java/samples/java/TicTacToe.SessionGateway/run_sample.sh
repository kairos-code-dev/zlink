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

gradle_run() {
  gradle --no-daemon "$@" --quiet
}

read -r registry_pub_port registry_router_port play_route_port api_port play_port session_port < <(reserve_ports)
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Dzlink.samples.tictactoe.sessiongateway.registryPubEndpoint=tcp://127.0.0.1:${registry_pub_port} -Dzlink.samples.tictactoe.sessiongateway.registryRouterEndpoint=tcp://127.0.0.1:${registry_router_port} -Dzlink.samples.tictactoe.sessiongateway.playRouteEndpoint=tcp://127.0.0.1:${play_route_port} -Dzlink.samples.tictactoe.sessiongateway.apiEndpoint=tcp://127.0.0.1:${api_port} -Dzlink.samples.tictactoe.sessiongateway.playEndpoint=tcp://127.0.0.1:${play_port} -Dzlink.samples.tictactoe.sessiongateway.sessionEndpoint=tcp://127.0.0.1:${session_port}"

gradle_run :Server:Registry:run &
pids+=("$!")
wait_port "${registry_router_port}"
gradle_run :Server:Api:run &
pids+=("$!")
wait_port "${api_port}"
gradle_run :Server:Play:run &
pids+=("$!")
wait_port "${play_port}"
gradle_run :Server:Session:run &
pids+=("$!")
wait_port "${session_port}"

gradle_run :Client:run
