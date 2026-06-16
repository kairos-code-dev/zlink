#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.samples\.deliverydispatch\.(server\.(registry|dispatchapi|dispatchcenter|courier|tracking|session)\.Program|client\.Program|probe\.Program)'
log_dir="build/sample-logs"
work_dir="build/sample-state"
mkdir -p "${log_dir}"
rm -f "${log_dir}"/*.log
rm -rf "${work_dir}"
mkdir -p "${work_dir}"

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
  pgrep -P "${pid}" 2>/dev/null | while read -r child; do
    descendants "${child}"
    echo "${child}"
  done
}

kill_role_processes() {
  pgrep -f "${role_pattern}" 2>/dev/null | while read -r pid; do
    kill "${pid}" >/dev/null 2>&1 || true
  done
}

kill_role_processes_forcibly() {
  pgrep -f "${role_pattern}" 2>/dev/null | while read -r pid; do
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
    while len(reserved) < 15:
        host = "127.0.0.1"
        port = random.randint(20000, 32767)
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

build_framework_jars() {
  (
    cd ../../..
    ./gradlew --no-daemon \
      :zlink-framework-core:jar \
      :zlink-framework-spring-boot-starter:jar \
      :zlink-stream-connector:jar \
      :zlink-stream-connector-json:jar \
      --quiet
  )
}

read -r registry_pub registry_router api_channel dispatch_channel courier_a courier_b tracking_channel status_fanout tracking_spot_router tracking_spot_pub session_stream session_spot_router session_spot_pub tracking_spot_route session_spot_route < <(reserve_ports)
registry_pub_host="${registry_pub%:*}"; registry_pub_port="${registry_pub##*:}"
registry_router_host="${registry_router%:*}"; registry_router_port="${registry_router##*:}"
api_host="${api_channel%:*}"; api_port="${api_channel##*:}"
dispatch_host="${dispatch_channel%:*}"; dispatch_port="${dispatch_channel##*:}"
courier_a_host="${courier_a%:*}"; courier_a_port="${courier_a##*:}"
courier_b_host="${courier_b%:*}"; courier_b_port="${courier_b##*:}"
tracking_host="${tracking_channel%:*}"; tracking_port="${tracking_channel##*:}"
status_host="${status_fanout%:*}"; status_port="${status_fanout##*:}"
tracking_spot_router_host="${tracking_spot_router%:*}"; tracking_spot_router_port="${tracking_spot_router##*:}"
tracking_spot_pub_host="${tracking_spot_pub%:*}"; tracking_spot_pub_port="${tracking_spot_pub##*:}"
session_stream_host="${session_stream%:*}"; session_stream_port="${session_stream##*:}"
session_spot_router_host="${session_spot_router%:*}"; session_spot_router_port="${session_spot_router##*:}"
session_spot_pub_host="${session_spot_pub%:*}"; session_spot_pub_port="${session_spot_pub##*:}"
tracking_spot_route_host="${tracking_spot_route%:*}"; tracking_spot_route_port="${tracking_spot_route##*:}"
session_spot_route_host="${session_spot_route%:*}"; session_spot_route_port="${session_spot_route##*:}"

prefix="zlink.samples.deliverydispatch"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} \
-D${prefix}.registryPubEndpoint=tcp://${registry_pub_host}:${registry_pub_port} \
-D${prefix}.registryRouterEndpoint=tcp://${registry_router_host}:${registry_router_port} \
-D${prefix}.apiChannelEndpoint=tcp://${api_host}:${api_port} \
-D${prefix}.dispatchChannelEndpoint=tcp://${dispatch_host}:${dispatch_port} \
-D${prefix}.courierAEndpoint=tcp://${courier_a_host}:${courier_a_port} \
-D${prefix}.courierBEndpoint=tcp://${courier_b_host}:${courier_b_port} \
-D${prefix}.trackingChannelEndpoint=tcp://${tracking_host}:${tracking_port} \
-D${prefix}.statusFanoutEndpoint=tcp://${status_host}:${status_port} \
-D${prefix}.trackingSpotRouterEndpoint=tcp://${tracking_spot_router_host}:${tracking_spot_router_port} \
-D${prefix}.trackingSpotEndpoint=tcp://${tracking_spot_pub_host}:${tracking_spot_pub_port} \
-D${prefix}.sessionStreamEndpoint=tcp://${session_stream_host}:${session_stream_port} \
-D${prefix}.sessionSpotRouterEndpoint=tcp://${session_spot_router_host}:${session_spot_router_port} \
-D${prefix}.sessionSpotEndpoint=tcp://${session_spot_pub_host}:${session_spot_pub_port} \
-D${prefix}.trackingSpotRouteEndpoint=tcp://${tracking_spot_route_host}:${tracking_spot_route_port} \
-D${prefix}.sessionSpotRouteEndpoint=tcp://${session_spot_route_host}:${session_spot_route_port} \
-D${prefix}.workDir=$(pwd)/${work_dir}"

build_framework_jars
gradle_run classes

gradle_run :Server:Registry:run >"${log_dir}/registry.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/registry.log" "Started Program"

gradle_run :Server:Tracking:run >"${log_dir}/tracking.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/tracking.log" "Started Program"

gradle_run :Server:Session:run >"${log_dir}/session.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/session.log" "Started Program"

gradle_run :Server:Courier:run --args="--courier courier-a --mode timeout-reassign" >"${log_dir}/courier-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/courier-a.log" "Started Program"

gradle_run :Server:Courier:run --args="--courier courier-b --mode accept" >"${log_dir}/courier-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/courier-b.log" "Started Program"

gradle_run :Server:DispatchCenter:run >"${log_dir}/dispatch-center.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/dispatch-center.log" "Started Program"

gradle_run :Server:DispatchApi:run >"${log_dir}/dispatch-api.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/dispatch-api.log" "Started Program"

gradle_run :Probe:run >"${log_dir}/probe.log" 2>&1
gradle_run :Client:run >"${log_dir}/client.log" 2>&1
