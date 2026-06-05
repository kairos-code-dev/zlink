#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.samples\.kotlin\.tictactoe\.sessiongateway\.(server|client)'
log_dir="build/sample-logs"
mkdir -p "${log_dir}"
rm -f "${log_dir}"/*.log

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for log in "${log_dir}"/*.log; do
    [[ -f "${log}" ]] || continue
    echo "===== ${log} =====" >&2
    tail -n 200 "${log}" >&2 || true
  done
}

descendants() {
  local pid="$1"
  local child
  (pgrep -P "${pid}" 2>/dev/null || true) | while read -r child; do
    descendants "${child}"
    echo "${child}"
  done
}

kill_role_processes() {
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill "${pid}" >/dev/null 2>&1 || true
  done
}

kill_role_processes_forcibly() {
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
}

cleanup() {
  local status="$?"
  print_logs "${status}"
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    local pid="${pids[$i]}"
    for child in $(descendants "${pid}"); do
      kill "${child}" >/dev/null 2>&1 || true
    done
    kill "${pid}" >/dev/null 2>&1 || true
  done
  kill_role_processes
  for _ in $(seq 1 20); do
    local any_alive=0
    for pid in "${pids[@]}"; do
      if kill -0 "${pid}" >/dev/null 2>&1; then
        any_alive=1
        break
      fi
      for child in $(descendants "${pid}"); do
        if kill -0 "${child}" >/dev/null 2>&1; then
          any_alive=1
          break
        fi
      done
    done
    if [[ "${any_alive}" == "0" ]]; then
      break
    fi
    sleep 0.1
  done
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    local pid="${pids[$i]}"
    for child in $(descendants "${pid}"); do
      kill -9 "${child}" >/dev/null 2>&1 || true
    done
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
  kill_role_processes_forcibly
  for pid in "${pids[@]}"; do
    wait "${pid}" 2>/dev/null || true
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
import random
import socket
reserved = []
ports = []
try:
    chosen = set()
    while len(reserved) < 9:
        port = random.randint(20000, 32767)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        reserved.append(sock)
        ports.append(str(port))
    print(" ".join(ports))
finally:
    for sock in reserved:
        sock.close()
PY
}

gradle_run() {
  ../../gradlew --no-daemon "$@" --quiet
}

read -r registry_pub_port registry_router_port play_route_port play_spot_router_port session_route_port session_router_port api_port play_port session_port < <(reserve_ports)
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Dzlink.samples.tictactoe.sessiongateway.registryPubEndpoint=tcp://127.0.0.1:${registry_pub_port} -Dzlink.samples.tictactoe.sessiongateway.registryRouterEndpoint=tcp://127.0.0.1:${registry_router_port} -Dzlink.samples.tictactoe.sessiongateway.playRouteEndpoint=tcp://127.0.0.1:${play_route_port} -Dzlink.samples.tictactoe.sessiongateway.playSpotRouterEndpoint=tcp://127.0.0.1:${play_spot_router_port} -Dzlink.samples.tictactoe.sessiongateway.sessionRouteEndpoint=tcp://127.0.0.1:${session_route_port} -Dzlink.samples.tictactoe.sessiongateway.sessionRouterEndpoint=tcp://127.0.0.1:${session_router_port} -Dzlink.samples.tictactoe.sessiongateway.apiEndpoint=tcp://127.0.0.1:${api_port} -Dzlink.samples.tictactoe.sessiongateway.playEndpoint=tcp://127.0.0.1:${play_port} -Dzlink.samples.tictactoe.sessiongateway.sessionEndpoint=tcp://127.0.0.1:${session_port}"

gradle_run :Server:Registry:run >"${log_dir}/registry.log" 2>&1 &
pids+=("$!")
wait_port "${registry_router_port}"
wait_port "${registry_pub_port}"
gradle_run :Server:Api:run >"${log_dir}/api.log" 2>&1 &
pids+=("$!")
wait_port "${api_port}"
gradle_run :Server:Play:run >"${log_dir}/play.log" 2>&1 &
pids+=("$!")
wait_port "${play_port}"
wait_port "${play_route_port}"
wait_port "${play_spot_router_port}"
gradle_run :Server:Session:run >"${log_dir}/session.log" 2>&1 &
pids+=("$!")
wait_port "${session_port}"
wait_port "${session_route_port}"
wait_port "${session_router_port}"

gradle_run :Client:run >"${log_dir}/client.log" 2>&1
