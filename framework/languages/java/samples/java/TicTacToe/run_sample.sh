#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

core_lib="$(cd ../../../../../.. && pwd)/core/build/lib/libzlink.so"
if [[ -f "${core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${core_lib}"
fi

pids=()
redis_container_id=""
redis_key_prefix="zlink:tictactoe:${RANDOM}:$$:room:"
role_pattern='systems\.zlink\.samples\.tictactoe\.server\.Program|systems\.zlink\.samples\.tictactoe\.client\.Program'
run_dir="$(mktemp -d)"
log_dir="${run_dir}/logs"
export TICTACTOE_LOG_DIR="${TICTACTOE_LOG_DIR:-$(pwd)/logs}"
mkdir -p "${log_dir}" "${TICTACTOE_LOG_DIR}"
rm -f "${TICTACTOE_LOG_DIR}"/*.log

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
  if [[ -n "${redis_container_id}" ]]; then
    docker rm -f "${redis_container_id}" >/dev/null 2>&1 || true
  fi
  if [[ "${TICTACTOE_JAVA_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${run_dir}"
  else
    rm -rf "${run_dir}"
  fi
  return "${status}"
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

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#redis://}"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#redis://}"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_endpoint() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  local deadline=$((SECONDS + 45))
  while (( SECONDS < deadline )); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
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
    while len(reserved) < 15:
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
        reserved.append(sock)
        ports.append(str(port))
    print(" ".join(ports))
finally:
    for sock in reserved:
        sock.close()
PY
}

gradle_run() {
  ../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts --no-daemon "$@" --quiet
}

app_bin() {
  local project_path="$1"
  local app_name="$2"
  printf '%s/build/install/%s/bin/%s' "${project_path}" "${app_name}" "${app_name}"
}

read -r api_a_http_port api_b_http_port api_a_channel_port api_b_channel_port play_a_channel_port play_b_channel_port play_a_stream_port play_b_stream_port play_a_spot_port play_b_spot_port play_a_route_port play_b_route_port play_a_pub_port play_b_pub_port redis_port < <(reserve_ports)

# The sample owns its Redis: always provision a dedicated, throwaway container
# so room-route state stays isolated per run and never touches a developer's
# local Redis. (redis_endpoint is intentionally derived here, not read from env.)
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the TicTacToe sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
redis_container_id="$(docker run -d --rm \
  --name "zlink-tictactoe-java-redis-${RANDOM}-$$" \
  --label "systems.zlink.sample=tictactoe-java" \
  -p "127.0.0.1::6379" \
  redis:7-alpine)"
redis_endpoint="$(docker port "${redis_container_id}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"

api_a_config="${run_dir}/sample.api-a.properties"
api_b_config="${run_dir}/sample.api-b.properties"
play_a_config="${run_dir}/sample.play-a.properties"
play_b_config="${run_dir}/sample.play-b.properties"

common_play_channels="tcp://127.0.0.1:${play_a_channel_port},tcp://127.0.0.1:${play_b_channel_port}"
common_play_streams="tcp://127.0.0.1:${play_a_stream_port},tcp://127.0.0.1:${play_b_stream_port}"
common_spots="tcp://127.0.0.1:${play_a_spot_port},tcp://127.0.0.1:${play_b_spot_port}"
common_routes="tcp://127.0.0.1:${play_a_route_port},tcp://127.0.0.1:${play_b_route_port}"
common_pubs="tcp://127.0.0.1:${play_a_pub_port},tcp://127.0.0.1:${play_b_pub_port}"

cat >"${api_a_config}" <<EOF
sample.apiBindUrl=http://127.0.0.1:${api_a_http_port}
sample.apiPublicUrl=http://127.0.0.1:${api_a_http_port}
sample.apiChannelEndpoint=tcp://127.0.0.1:${api_a_channel_port}
sample.playChannelEndpoint=tcp://127.0.0.1:${play_a_channel_port}
sample.playChannelEndpoints=${common_play_channels}
sample.playEndpoint=tcp://127.0.0.1:${play_a_stream_port}
sample.playEndpoints=${common_play_streams}
sample.spotEndpoint=tcp://127.0.0.1:${play_a_spot_port}
sample.spotEndpoints=${common_spots}
sample.routeEndpoint=tcp://127.0.0.1:${play_a_route_port}
sample.routeEndpoints=${common_routes}
sample.spotPubSubEndpoint=tcp://127.0.0.1:${play_a_pub_port}
sample.spotPubSubEndpoints=${common_pubs}
sample.redisEndpoint=${redis_endpoint}
sample.redisKeyPrefix=${redis_key_prefix}
sample.playSpotNodeRid=play-node-1
sample.peerPlaySpotNodeRid=play-node-2
sample.peerSpotEndpoint=tcp://127.0.0.1:${play_b_spot_port}
sample.peerRouteEndpoint=tcp://127.0.0.1:${play_b_route_port}
sample.peerSpotPubSubEndpoint=tcp://127.0.0.1:${play_b_pub_port}
sample.logDirectory=${log_dir}
EOF

cat >"${api_b_config}" <<EOF
sample.apiBindUrl=http://127.0.0.1:${api_b_http_port}
sample.apiPublicUrl=http://127.0.0.1:${api_b_http_port}
sample.apiChannelEndpoint=tcp://127.0.0.1:${api_b_channel_port}
sample.playChannelEndpoint=tcp://127.0.0.1:${play_b_channel_port}
sample.playChannelEndpoints=${common_play_channels}
sample.playEndpoint=tcp://127.0.0.1:${play_b_stream_port}
sample.playEndpoints=${common_play_streams}
sample.spotEndpoint=tcp://127.0.0.1:${play_b_spot_port}
sample.spotEndpoints=${common_spots}
sample.routeEndpoint=tcp://127.0.0.1:${play_b_route_port}
sample.routeEndpoints=${common_routes}
sample.spotPubSubEndpoint=tcp://127.0.0.1:${play_b_pub_port}
sample.spotPubSubEndpoints=${common_pubs}
sample.redisEndpoint=${redis_endpoint}
sample.redisKeyPrefix=${redis_key_prefix}
sample.playSpotNodeRid=play-node-2
sample.peerPlaySpotNodeRid=play-node-1
sample.peerSpotEndpoint=tcp://127.0.0.1:${play_a_spot_port}
sample.peerRouteEndpoint=tcp://127.0.0.1:${play_a_route_port}
sample.peerSpotPubSubEndpoint=tcp://127.0.0.1:${play_a_pub_port}
sample.logDirectory=${log_dir}
EOF

cp "${api_a_config}" "${play_a_config}"
cp "${api_b_config}" "${play_b_config}"
{
  echo "sample.apiChannelEndpoint=tcp://127.0.0.1:${api_a_channel_port}"
  echo "sample.playChannelEndpoint=tcp://127.0.0.1:${play_a_channel_port}"
} >>"${play_a_config}"
{
  echo "sample.apiChannelEndpoint=tcp://127.0.0.1:${api_a_channel_port}"
  echo "sample.playChannelEndpoint=tcp://127.0.0.1:${play_b_channel_port}"
} >>"${play_b_config}"

wait_endpoint redis "${redis_endpoint}"

gradle_run :Server:installDist :Client:installDist

"$(app_bin Server Server)" play --config "${play_a_config}" >"${log_dir}/play-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play-a.log" "Started Program"

"$(app_bin Server Server)" play --config "${play_b_config}" >"${log_dir}/play-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play-b.log" "Started Program"

"$(app_bin Server Server)" api --config "${api_a_config}" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api-a.log" "Started Program"

"$(app_bin Server Server)" api --config "${api_b_config}" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api-b.log" "Started Program"

"$(app_bin Client Client)" --api-url "http://127.0.0.1:${api_a_http_port}" >"${log_dir}/client.log" 2>&1

rg -q "observer-connected endpoint=tcp://127.0.0.1:${play_b_stream_port}" "${log_dir}/client.log"
rg -q "observer-subscription=verified subscribed=true" "${log_dir}/client.log"
rg -q "observer-win-milestone=verified actor=player-x wins=100 receivingSpotNodeRid=play-node-2" "${log_dir}/client.log"
rg -q "tictactoe completed" "${log_dir}/client.log"
grep -Rq "message flow" "${TICTACTOE_LOG_DIR}"
