#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.samples\.supportchat\.(server\.(registry|api|support|session)\.Program|client\.Program|probe\.Program)'
log_dir="build/sample-logs"
export SUPPORTCHAT_LOG_DIR="${SUPPORTCHAT_LOG_DIR:-$(pwd)/logs}"
mkdir -p "${log_dir}" "${SUPPORTCHAT_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${SUPPORTCHAT_LOG_DIR}"/*.log

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
  set +e
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
  return "${status}"
}
trap cleanup EXIT

wait_port() {
  local host="$1"
  local port="$2"
  local deadline=$((SECONDS + 60))
  while (( SECONDS < deadline )); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${host}:${port}" >&2
  return 1
}

wait_log_contains() {
  local log_file="$1"
  local pattern="$2"
  local deadline=$((SECONDS + 60))
  while (( SECONDS < deadline )); do
    if [[ -f "${log_file}" ]] && grep -q "${pattern}" "${log_file}"; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${log_file} to contain ${pattern}" >&2
  return 1
}

reserve_ports() {
  python3 - <<'PY'
import random
import socket
reserved = []
try:
    chosen = set()
    while len(reserved) < 11:
        host = "127.0.0.1"
        port = random.randint(48000, 60999)
        key = (host, port)
        if key in chosen:
            continue
        sockets = []
        try:
            sock4 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock4.bind((host, port))
            sockets.append(sock4)
        except OSError:
            for sock in sockets:
                sock.close()
            continue
        chosen.add(key)
        reserved.append((host, port, sockets))
    print(" ".join(f"{host}:{port}" for host, port, _ in reserved))
finally:
    for _, _, sockets in reserved:
        for sock in sockets:
            sock.close()
PY
}

gradle_run() {
  ../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon "$@" --quiet
}

app_bin() {
  local project="$1"
  local script="$2"
  echo "${project}/build/install/${script}/bin/${script}"
}

build_framework_jars() {
  (
    cd ../../..
    ./gradlew --no-daemon \
      :zlink-framework-core:jar \
      :zlink-framework-spring-boot-starter:jar \
      :zlink-framework-codec-protobuf:jar \
      :zlink-stream-connector:jar \
      --quiet
  )
}

read -r registry_pub registry_router api_channel support_channel session_spot session_router support_spot support_router session_route support_route stream_endpoint < <(reserve_ports)
registry_pub_host="${registry_pub%:*}"
registry_pub_port="${registry_pub##*:}"
registry_router_host="${registry_router%:*}"
registry_router_port="${registry_router##*:}"
api_host="${api_channel%:*}"
api_port="${api_channel##*:}"
support_channel_host="${support_channel%:*}"
support_channel_port="${support_channel##*:}"
session_spot_host="${session_spot%:*}"
session_spot_port="${session_spot##*:}"
session_router_host="${session_router%:*}"
session_router_port="${session_router##*:}"
support_spot_host="${support_spot%:*}"
support_spot_port="${support_spot##*:}"
support_router_host="${support_router%:*}"
support_router_port="${support_router##*:}"
session_route_host="${session_route%:*}"
session_route_port="${session_route##*:}"
support_route_host="${support_route%:*}"
support_route_port="${support_route##*:}"
stream_host="${stream_endpoint%:*}"
stream_port="${stream_endpoint##*:}"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Dzlink.samples.supportchat.registryPubEndpoint=tcp://${registry_pub_host}:${registry_pub_port} -Dzlink.samples.supportchat.registryRouterEndpoint=tcp://${registry_router_host}:${registry_router_port} -Dzlink.samples.supportchat.apiChannelEndpoint=tcp://${api_host}:${api_port} -Dzlink.samples.supportchat.supportChannelEndpoint=tcp://${support_channel_host}:${support_channel_port} -Dzlink.samples.supportchat.sessionSpotEndpoint=tcp://${session_spot_host}:${session_spot_port} -Dzlink.samples.supportchat.sessionRouterEndpoint=tcp://${session_router_host}:${session_router_port} -Dzlink.samples.supportchat.supportSpotEndpoint=tcp://${support_spot_host}:${support_spot_port} -Dzlink.samples.supportchat.supportSpotRouterEndpoint=tcp://${support_router_host}:${support_router_port} -Dzlink.samples.supportchat.sessionRouteEndpoint=tcp://${session_route_host}:${session_route_port} -Dzlink.samples.supportchat.supportRouteEndpoint=tcp://${support_route_host}:${support_route_port} -Dzlink.samples.supportchat.streamEndpoint=tcp://${stream_host}:${stream_port}"

build_framework_jars
gradle_run \
  :Server:Registry:installDist \
  :Server:Session:installDist \
  :Server:Api:installDist \
  :Server:Support:installDist \
  :Probe:installDist \
  :Client:installDist

"$(app_bin Server/Registry Registry)" >"${log_dir}/registry.log" 2>&1 &
pids+=("$!")
wait_port "${registry_pub_host}" "${registry_pub_port}"
wait_port "${registry_router_host}" "${registry_router_port}"
wait_log_contains "${log_dir}/registry.log" "Started Program"
"$(app_bin Server/Session Session)" >"${log_dir}/session.log" 2>&1 &
pids+=("$!")
wait_port "${session_route_host}" "${session_route_port}"
wait_port "${session_spot_host}" "${session_spot_port}"
wait_port "${session_router_host}" "${session_router_port}"
wait_port "${stream_host}" "${stream_port}"
wait_log_contains "${log_dir}/session.log" "Started Program"
"$(app_bin Server/Api Api)" >"${log_dir}/api.log" 2>&1 &
pids+=("$!")
wait_port "${api_host}" "${api_port}"
wait_log_contains "${log_dir}/api.log" "Started Program"
"$(app_bin Server/Support Support)" >"${log_dir}/support.log" 2>&1 &
pids+=("$!")
wait_port "${support_channel_host}" "${support_channel_port}"
wait_port "${support_route_host}" "${support_route_port}"
wait_port "${support_router_host}" "${support_router_port}"
wait_port "${support_spot_host}" "${support_spot_port}"
wait_log_contains "${log_dir}/support.log" "Started Program"
sleep 2

"$(app_bin Probe Probe)" >"${log_dir}/probe.log" 2>&1
"$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1
grep -Rq "message flow" "${SUPPORTCHAT_LOG_DIR}"
