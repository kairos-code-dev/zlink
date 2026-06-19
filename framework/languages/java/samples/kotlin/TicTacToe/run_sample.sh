#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
redis_container_id=""
redis_key_prefix="zlink:tictactoe-kotlin:${RANDOM}:$$:room:"
role_pattern='systems\.zlink\.samples\.kotlin\.tictactoe\.server\.ProgramKt|systems\.zlink\.samples\.kotlin\.tictactoe\.client\.ProgramKt'
run_dir="$(mktemp -d)"
log_dir="${run_dir}/logs"
mkdir -p "${log_dir}"

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for log in "${log_dir}"/*.log; do
    [[ -f "${log}" ]] || continue
    echo "===== ${log} =====" >&2
    tail -n 240 "${log}" >&2 || true
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
  sleep 0.5
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
  if [[ "${TICTACTOE_KOTLIN_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${run_dir}"
  else
    rm -rf "${run_dir}"
  fi
  return "${status}"
}
trap cleanup EXIT

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint##*:}"
}

wait_endpoint() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 150); do
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

wait_grep() {
  local name="$1"
  local pattern="$2"
  shift 2
  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if rg -q "${pattern}" "$@" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name}" >&2
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

read -r api_a_http_port api_b_http_port api_a_channel_port api_b_channel_port play_a_channel_port play_b_channel_port play_a_stream_port play_b_stream_port play_a_spot_port play_b_spot_port play_a_route_port play_b_route_port play_a_pub_port play_b_pub_port redis_port < <(reserve_ports)

if [[ -z "${TICTACTOE_REDIS_ENDPOINT:-}" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "TICTACTOE_REDIS_ENDPOINT is not set and docker is not available." >&2
    exit 1
  fi
  redis_container_id="$(docker run -d --rm \
    --name "zlink-tictactoe-kotlin-redis-${redis_port}-$$" \
    --label "systems.zlink.sample=tictactoe-kotlin" \
    -p "127.0.0.1:${redis_port}:6379" \
    redis:7-alpine)"
  redis_endpoint="127.0.0.1:${redis_port}"
else
  redis_endpoint="${TICTACTOE_REDIS_ENDPOINT}"
fi

wait_endpoint redis "${redis_endpoint}"

common_play_channels="tcp://127.0.0.1:${play_a_channel_port},tcp://127.0.0.1:${play_b_channel_port}"
common_play_streams="tcp://127.0.0.1:${play_a_stream_port},tcp://127.0.0.1:${play_b_stream_port}"
common_spots="tcp://127.0.0.1:${play_a_spot_port},tcp://127.0.0.1:${play_b_spot_port}"
common_routes="tcp://127.0.0.1:${play_a_route_port},tcp://127.0.0.1:${play_b_route_port}"
common_pubs="tcp://127.0.0.1:${play_a_pub_port},tcp://127.0.0.1:${play_b_pub_port}"

api_a_config="${run_dir}/api-a.properties"
api_b_config="${run_dir}/api-b.properties"
play_a_config="${run_dir}/play-a.properties"
play_b_config="${run_dir}/play-b.properties"

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

cp "${api_a_config}" "${api_b_config}"
sed -i \
  -e "s#sample.apiBindUrl=.*#sample.apiBindUrl=http://127.0.0.1:${api_b_http_port}#" \
  -e "s#sample.apiPublicUrl=.*#sample.apiPublicUrl=http://127.0.0.1:${api_b_http_port}#" \
  -e "s#sample.apiChannelEndpoint=.*#sample.apiChannelEndpoint=tcp://127.0.0.1:${api_b_channel_port}#" \
  "${api_b_config}"

cp "${api_a_config}" "${play_a_config}"
cp "${api_a_config}" "${play_b_config}"
sed -i \
  -e "s#sample.playChannelEndpoint=.*#sample.playChannelEndpoint=tcp://127.0.0.1:${play_b_channel_port}#" \
  -e "s#sample.playEndpoint=.*#sample.playEndpoint=tcp://127.0.0.1:${play_b_stream_port}#" \
  -e "s#sample.spotEndpoint=.*#sample.spotEndpoint=tcp://127.0.0.1:${play_b_spot_port}#" \
  -e "s#sample.routeEndpoint=.*#sample.routeEndpoint=tcp://127.0.0.1:${play_b_route_port}#" \
  -e "s#sample.spotPubSubEndpoint=.*#sample.spotPubSubEndpoint=tcp://127.0.0.1:${play_b_pub_port}#" \
  -e "s#sample.playSpotNodeRid=.*#sample.playSpotNodeRid=play-node-2#" \
  -e "s#sample.peerPlaySpotNodeRid=.*#sample.peerPlaySpotNodeRid=play-node-1#" \
  -e "s#sample.peerSpotEndpoint=.*#sample.peerSpotEndpoint=tcp://127.0.0.1:${play_a_spot_port}#" \
  -e "s#sample.peerRouteEndpoint=.*#sample.peerRouteEndpoint=tcp://127.0.0.1:${play_a_route_port}#" \
  -e "s#sample.peerSpotPubSubEndpoint=.*#sample.peerSpotPubSubEndpoint=tcp://127.0.0.1:${play_a_pub_port}#" \
  "${play_b_config}"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Server:run --quiet --args="play --config ${play_b_config}" >"${log_dir}/play-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play-b.log" "Started Program"
wait_endpoint play-b-stream "tcp://127.0.0.1:${play_b_stream_port}"
wait_endpoint play-b-spot "tcp://127.0.0.1:${play_b_spot_port}"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Server:run --quiet --args="play --config ${play_a_config}" >"${log_dir}/play-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play-a.log" "Started Program"
wait_endpoint play-a-stream "tcp://127.0.0.1:${play_a_stream_port}"
wait_endpoint play-a-spot "tcp://127.0.0.1:${play_a_spot_port}"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Server:run --quiet --args="api --config ${api_a_config}" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api-a.log" "Started Program"
wait_endpoint api-a-http "http://127.0.0.1:${api_a_http_port}"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Server:run --quiet --args="api --config ${api_b_config}" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api-b.log" "Started Program"
wait_endpoint api-b-http "http://127.0.0.1:${api_b_http_port}"

../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts :Client:run --quiet --args="--api-url http://127.0.0.1:${api_a_http_port}" >"${log_dir}/client.log" 2>&1

rg -q "observer-connected endpoint=tcp://127.0.0.1:${play_b_stream_port}" "${log_dir}/client.log"
rg -q "observer-subscription=verified subscribed=true" "${log_dir}/client.log"
rg -q "observer-win-milestone=verified actor=player-x wins=100 receivingSpotNodeRid=play-node-2" "${log_dir}/client.log"
rg -q "tictactoe completed" "${log_dir}/client.log"
wait_grep "host leave marker" "actor: LeaveGameReq completed. actor=player-x" "${log_dir}"/play-*.log
wait_grep "guest leave marker" "actor: LeaveGameReq completed. actor=player-o" "${log_dir}"/play-*.log
echo "PASS TicTacToe.Kotlin"
