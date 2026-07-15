#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

client_source="Client/src/main/kotlin/systems/zlink/samples/kotlin/bingo/client/BingoClientScenario.kt"
for assertion in \
  'client1Card.state.players.all { player -> player.card.size == 9 }' \
  'client2Drawn.state == client1Drawn.state' \
  'reward.drawSeq == drawnNumbers.last().drawSeq'; do
  if ! grep -Fq "${assertion}" "${client_source}"; then
    echo "Bingo client release assertion missing: ${assertion}" >&2
    exit 1
  fi
done

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

pids=()
redis_container_id=""
log_dir="build/sample-logs"
export BINGO_LOG_DIR="${BINGO_LOG_DIR:-$(pwd)/logs}"
export ZLINK_JAVA_STREAM_TRACE="${ZLINK_JAVA_STREAM_TRACE:-1}"
mkdir -p "${log_dir}" "${BINGO_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${BINGO_LOG_DIR}"/*.log

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

reserve_ports() {
  local base=$((48000 + ((RANDOM + $$) % 1000) * 15 % 12000))
  local endpoints=()
  for offset in $(seq 0 14); do
    endpoints+=("127.0.0.1:$((base + offset))")
  done
  echo "${endpoints[*]}"
}

build_framework_jars() {
  (
    cd ../../..
    ./gradlew --no-daemon \
      :zlink-framework-core:jar \
      :zlink-framework-spring-boot-starter:jar \
      :zlink-framework-kotlin:jar \
      :zlink-framework-locations-redis:jar \
      :zlink-framework-codec-protobuf:jar \
      :zlink-stream-connector:jar \
      --quiet
  )
}

read -r api_a_channel play_a_channel session_a_spot session_a_router play_a_spot play_a_router session_a_stream api_b_channel play_b_channel session_b_spot session_b_router play_b_spot play_b_router session_b_stream unused_endpoint < <(reserve_ports)
api_a_host="${api_a_channel%:*}"
api_a_port="${api_a_channel##*:}"
api_b_host="${api_b_channel%:*}"
api_b_port="${api_b_channel##*:}"
play_a_host="${play_a_channel%:*}"
play_a_port="${play_a_channel##*:}"
play_b_host="${play_b_channel%:*}"
play_b_port="${play_b_channel##*:}"
session_a_spot_host="${session_a_spot%:*}"
session_a_spot_port="${session_a_spot##*:}"
session_b_spot_host="${session_b_spot%:*}"
session_b_spot_port="${session_b_spot##*:}"
session_a_router_host="${session_a_router%:*}"
session_a_router_port="${session_a_router##*:}"
session_b_router_host="${session_b_router%:*}"
session_b_router_port="${session_b_router##*:}"
play_a_spot_host="${play_a_spot%:*}"
play_a_spot_port="${play_a_spot##*:}"
play_b_spot_host="${play_b_spot%:*}"
play_b_spot_port="${play_b_spot##*:}"
play_a_router_host="${play_a_router%:*}"
play_a_router_port="${play_a_router##*:}"
play_b_router_host="${play_b_router%:*}"
play_b_router_port="${play_b_router##*:}"
stream_a_host="${session_a_stream%:*}"
stream_a_port="${session_a_stream##*:}"
stream_b_host="${session_b_stream%:*}"
stream_b_port="${session_b_stream##*:}"
bingo_redis_key_prefix="${BINGO_REDIS_KEY_PREFIX:-bingo:kotlin:${RANDOM}:$$:}"
zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-kotlin-sample-bingo" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
BINGO_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
redis_host="${BINGO_REDIS_ENDPOINT%:*}"
redis_port="${BINGO_REDIS_ENDPOINT##*:}"
wait_port "${redis_host}" "${redis_port}"
common_java_options="${JAVA_TOOL_OPTIONS:-} -Dzlink.samples.bingo.apiAChannelEndpoint=tcp://${api_a_host}:${api_a_port} -Dzlink.samples.bingo.apiBChannelEndpoint=tcp://${api_b_host}:${api_b_port} -Dzlink.samples.bingo.playAChannelEndpoint=tcp://${play_a_host}:${play_a_port} -Dzlink.samples.bingo.playBChannelEndpoint=tcp://${play_b_host}:${play_b_port} -Dzlink.samples.bingo.sessionASpotEndpoint=tcp://${session_a_spot_host}:${session_a_spot_port} -Dzlink.samples.bingo.sessionBSpotEndpoint=tcp://${session_b_spot_host}:${session_b_spot_port} -Dzlink.samples.bingo.sessionARouterEndpoint=tcp://${session_a_router_host}:${session_a_router_port} -Dzlink.samples.bingo.sessionBRouterEndpoint=tcp://${session_b_router_host}:${session_b_router_port} -Dzlink.samples.bingo.playASpotEndpoint=tcp://${play_a_spot_host}:${play_a_spot_port} -Dzlink.samples.bingo.playBSpotEndpoint=tcp://${play_b_spot_host}:${play_b_spot_port} -Dzlink.samples.bingo.playASpotRouterEndpoint=tcp://${play_a_router_host}:${play_a_router_port} -Dzlink.samples.bingo.playBSpotRouterEndpoint=tcp://${play_b_router_host}:${play_b_router_port} -Dzlink.samples.bingo.sessionAStreamEndpoint=tcp://${stream_a_host}:${stream_a_port} -Dzlink.samples.bingo.sessionBStreamEndpoint=tcp://${stream_b_host}:${stream_b_port} -Dzlink.samples.bingo.redisEndpoint=${BINGO_REDIS_ENDPOINT} -Dzlink.samples.bingo.redisKeyPrefix=${bingo_redis_key_prefix}"

build_framework_jars
gradle_run \
  :Server:Session:installDist \
  :Server:Api:installDist \
  :Server:Play:installDist \
  :Client:installDist
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.bingo.sessionNode=a" "$(app_bin Server/Session Session)" >"${log_dir}/session-a.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.bingo.sessionNode=b" "$(app_bin Server/Session Session)" >"${log_dir}/session-b.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.bingo.apiNode=a" "$(app_bin Server/Api Api)" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.bingo.apiNode=b" "$(app_bin Server/Api Api)" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.bingo.playNode=a -Dzlink.samples.bingo.playRid=2201" "$(app_bin Server/Play Play)" >"${log_dir}/play-a.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.bingo.playNode=b -Dzlink.samples.bingo.playRid=2202" "$(app_bin Server/Play Play)" >"${log_dir}/play-b.log" 2>&1 &
pids+=("$!")
wait_port "${session_a_router_host}" "${session_a_router_port}"
wait_port "${stream_a_host}" "${stream_a_port}"
wait_port "${session_b_router_host}" "${session_b_router_port}"
wait_port "${stream_b_host}" "${stream_b_port}"
wait_port "${api_a_host}" "${api_a_port}"
wait_port "${api_b_host}" "${api_b_port}"
wait_port "${play_a_router_host}" "${play_a_router_port}"
wait_port "${play_a_spot_host}" "${play_a_spot_port}"
wait_port "${play_b_router_host}" "${play_b_router_port}"
wait_port "${play_b_spot_host}" "${play_b_spot_port}"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1

grep -q "bingo=completed" "${log_dir}/client.log"
grep -q "stream-inbound sample=Bingo" "${log_dir}/client.log"
grep -Rq "message flow" "${BINGO_LOG_DIR}"
grep -Eq "zlink metric .*name=zlink\.stream\.connections\.active" "${log_dir}"/session-*.log
grep -Eq "zlink metric .*name=zlink\.spot\.queue\.depth" "${log_dir}"/play-*.log

echo "bingo full client/server self-check completed"
