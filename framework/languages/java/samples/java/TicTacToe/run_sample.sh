#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.samples\.tictactoe\.server\.Program|systems\.zlink\.samples\.tictactoe\.client\.Program'
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

wait_log_contains() {
  local log_file="$1"
  local pattern="$2"
  local deadline=$((SECONDS + 60))
  while (( SECONDS < deadline )); do
    if [[ -f "${log_file}" ]] && rg -q "${pattern}" "${log_file}"; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for log pattern '${pattern}' in ${log_file}" >&2
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
    while len(reserved) < 5:
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

read -r api_port api_channel_port play_stream_port play_channel_port spot_port < <(reserve_ports)

config_file="$(pwd)/build/sample-application.properties"
cat >"${config_file}" <<EOF
sample.apiBindUrl=http://127.0.0.1:${api_port}
sample.apiPublicUrl=http://127.0.0.1:${api_port}
sample.apiChannelEndpoint=tcp://127.0.0.1:${api_channel_port}
sample.playChannelEndpoint=tcp://127.0.0.1:${play_channel_port}
sample.playEndpoint=tcp://127.0.0.1:${play_stream_port}
sample.spotEndpoint=tcp://127.0.0.1:${spot_port}
sample.logDirectory=${log_dir}
EOF

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Server:run --quiet --args="play --config ${config_file}" >"${log_dir}/play.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play.log" "Started Program"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Server:run --quiet --args="api --config ${config_file}" >"${log_dir}/api.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api.log" "Started Program"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Client:run --quiet --args="--api-url http://127.0.0.1:${api_port}" >"${log_dir}/client.log" 2>&1
