#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

core_lib="$(cd ../../../../../.. && pwd)/core/build/lib/libzlink.so"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${core_lib}"
fi

pids=()
redis_container_id=""
redis_key_prefix="zlink:tictactoe:${RANDOM}:$$:room:"
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
    tail -n 200 "${log}" >&2 || true
  done
}

trap cleanup EXIT

wait_endpoint() {
  local name="$1"
  local endpoint="$2"
  wait_port "${endpoint}" || {
    echo "Timed out waiting for ${name} at ${endpoint}" >&2
    return 1
  }
}

wait_log_contains() {
  local log_file="$1"
  local pattern="$2"
  local deadline=$((SECONDS + 60))
  while (( SECONDS < deadline )); do
    if [[ -f "${log_file}" ]] && grep -Eq "${pattern}" "${log_file}"; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for log pattern '${pattern}' in ${log_file}" >&2
  return 1
}

reserve_ports() {
  local base=$((48000 + ((RANDOM + $$) % 1000) * 13 % 12000))
  local ports=()
  for offset in $(seq 0 14); do
    ports+=("$((base + offset))")
  done
  echo "${ports[*]}"
}

read -r api_a_http_port api_b_http_port api_a_channel_port api_b_channel_port play_a_channel_port play_b_channel_port play_a_stream_port play_b_stream_port play_a_spot_port play_b_spot_port play_a_pub_port play_b_pub_port unused_port1 unused_port2 unused_port3 < <(reserve_ports)

zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-java-sample-tictactoe" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
redis_endpoint="127.0.0.1:${redis_port}"

wait_endpoint redis "${redis_endpoint}"

common_play_channels="tcp://127.0.0.1:${play_a_channel_port},tcp://127.0.0.1:${play_b_channel_port}"
common_play_streams="tcp://127.0.0.1:${play_a_stream_port},tcp://127.0.0.1:${play_b_stream_port}"
common_spots="tcp://127.0.0.1:${play_a_spot_port},tcp://127.0.0.1:${play_b_spot_port}"
common_pubs="tcp://127.0.0.1:${play_a_pub_port},tcp://127.0.0.1:${play_b_pub_port}"
play_a_node_rid="tic-play-alpha"
play_b_node_rid="tic-play-beta"

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
sample.spotPubSubEndpoint=tcp://127.0.0.1:${play_a_pub_port}
sample.spotPubSubEndpoints=${common_pubs}
sample.redisEndpoint=${redis_endpoint}
sample.redisKeyPrefix=${redis_key_prefix}
sample.playSpotNodeRid=${play_a_node_rid}
sample.peerPlaySpotNodeRid=${play_b_node_rid}
sample.peerSpotEndpoint=tcp://127.0.0.1:${play_b_spot_port}
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
  -e "s#sample.spotPubSubEndpoint=.*#sample.spotPubSubEndpoint=tcp://127.0.0.1:${play_b_pub_port}#" \
  -e "s#sample.playSpotNodeRid=.*#sample.playSpotNodeRid=${play_b_node_rid}#" \
  -e "s#sample.peerPlaySpotNodeRid=.*#sample.peerPlaySpotNodeRid=${play_a_node_rid}#" \
  -e "s#sample.peerSpotEndpoint=.*#sample.peerSpotEndpoint=tcp://127.0.0.1:${play_a_spot_port}#" \
  -e "s#sample.peerSpotPubSubEndpoint=.*#sample.peerSpotPubSubEndpoint=tcp://127.0.0.1:${play_a_pub_port}#" \
  "${play_b_config}"

chmod 0600 "${api_a_config}" "${api_b_config}" "${play_a_config}" "${play_b_config}"

gradle_run :Server:installDist :Client:installDist

Server/build/install/Server/bin/tictactoe-play --config "${play_b_config}" >"${log_dir}/play-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play-b.log" "Started PlayProgram"
wait_endpoint play-b-stream "tcp://127.0.0.1:${play_b_stream_port}"
wait_endpoint play-b-spot "tcp://127.0.0.1:${play_b_spot_port}"

Server/build/install/Server/bin/tictactoe-play --config "${play_a_config}" >"${log_dir}/play-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/play-a.log" "Started PlayProgram"
wait_endpoint play-a-stream "tcp://127.0.0.1:${play_a_stream_port}"
wait_endpoint play-a-spot "tcp://127.0.0.1:${play_a_spot_port}"

"$(app_bin Server Server)" --config "${api_a_config}" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api-a.log" "Started ApiProgram"
wait_endpoint api-a-http "http://127.0.0.1:${api_a_http_port}"

"$(app_bin Server Server)" --config "${api_b_config}" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_log_contains "${log_dir}/api-b.log" "Started ApiProgram"
wait_endpoint api-b-http "http://127.0.0.1:${api_b_http_port}"

"$(app_bin Client Client)" --api-url "http://127.0.0.1:${api_a_http_port}" >"${log_dir}/client.log" 2>&1

grep -Eq "observer-connected endpoint=tcp://127.0.0.1:${play_b_stream_port}" "${log_dir}/client.log"
grep -Eq "observer-subscription=verified subscribed=true" "${log_dir}/client.log"
grep -Eq "observer-win-milestone=verified actor=player-x wins=100 receivingSpotNodeRid=${play_b_node_rid}" "${log_dir}/client.log"
grep -Eq "tictactoe(=| )completed" "${log_dir}/client.log"
wait_log_contains "${log_dir}/play-a.log" "tictactoe actor destroy completed actor=player-x"
wait_log_contains "${log_dir}/play-a.log" "tictactoe actor destroy completed actor=player-o"
grep -Rq "message flow" "${log_dir}"
echo "PASS TicTacToe.Java"
