#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

pids=()
redis_container_id=""
log_dir="build/sample-logs"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-$(pwd)/logs}"
mkdir -p "${log_dir}" "${GAMEQUEST_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${GAMEQUEST_LOG_DIR}"/*.log

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

pause_briefly() {
  read -r -t 0.1 _ < <(:) || true
}

reserve_ports() {
  python3 - <<'PY'
import random
import socket
reserved = []
try:
    chosen = set()
    while len(reserved) < 8:
        host = "127.0.0.1"
        port = random.randint(20000, 29999)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind((host, port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        reserved.append((host, port, sock))
    print(" ".join(f"{host}:{port}" for host, port, _ in reserved))
finally:
    for _, _, sock in reserved:
        sock.close()
PY
}

build_framework_jars() {
  (
    cd ../../..
    ./gradlew --no-daemon \
      --no-parallel \
      --max-workers=1 \
      :zlink-framework-core:jar \
      :zlink-framework-kotlin:jar \
      :zlink-framework-spring-boot-starter:jar \
      :zlink-framework-locations-redis:jar \
      :zlink-stream-connector:jar \
      --quiet
  )
}

read -r api_a_stream api_b_stream api_a_http api_b_http mission_a_route mission_b_route mission_a_http mission_b_http < <(reserve_ports)

endpoint_host() { echo "${1%:*}"; }
endpoint_port() { echo "${1##*:}"; }

common_java_options="${JAVA_TOOL_OPTIONS:-}"
common_java_options+=" -Dzlink.samples.gamequest.apiAStreamEndpoint=tcp://$(endpoint_host "${api_a_stream}"):$(endpoint_port "${api_a_stream}")"
common_java_options+=" -Dzlink.samples.gamequest.apiBStreamEndpoint=tcp://$(endpoint_host "${api_b_stream}"):$(endpoint_port "${api_b_stream}")"
common_java_options+=" -Dzlink.samples.gamequest.apiAHttpEndpoint=http://$(endpoint_host "${api_a_http}"):$(endpoint_port "${api_a_http}")"
common_java_options+=" -Dzlink.samples.gamequest.apiBHttpEndpoint=http://$(endpoint_host "${api_b_http}"):$(endpoint_port "${api_b_http}")"
common_java_options+=" -Dzlink.samples.gamequest.missionARouteEndpoint=tcp://$(endpoint_host "${mission_a_route}"):$(endpoint_port "${mission_a_route}")"
common_java_options+=" -Dzlink.samples.gamequest.missionBRouteEndpoint=tcp://$(endpoint_host "${mission_b_route}"):$(endpoint_port "${mission_b_route}")"
common_java_options+=" -Dzlink.samples.gamequest.missionAHttpEndpoint=http://$(endpoint_host "${mission_a_http}"):$(endpoint_port "${mission_a_http}")"
common_java_options+=" -Dzlink.samples.gamequest.missionBHttpEndpoint=http://$(endpoint_host "${mission_b_http}"):$(endpoint_port "${mission_b_http}")"

gamequest_redis_key_prefix="${GAMEQUEST_REDIS_KEY_PREFIX:-gamequest:kotlin:${RANDOM}:$$:}"
zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-kotlin-sample-gamequest" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
GAMEQUEST_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
wait_port "${GAMEQUEST_REDIS_ENDPOINT%:*}" "${GAMEQUEST_REDIS_ENDPOINT##*:}"
common_java_options+=" -Dzlink.samples.gamequest.redisEndpoint=${GAMEQUEST_REDIS_ENDPOINT}"
common_java_options+=" -Dzlink.samples.gamequest.redisKeyPrefix=${gamequest_redis_key_prefix}"

build_framework_jars
gradle_run \
  :Server:GameApi:installDist \
  :Server:QuestMission:installDist \
  :Client:installDist

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.gamequest.missionName=mission-a" "$(app_bin Server/QuestMission QuestMission)" >"${log_dir}/mission-a.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.gamequest.missionName=mission-b" "$(app_bin Server/QuestMission QuestMission)" >"${log_dir}/mission-b.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${mission_a_route}")" "$(endpoint_port "${mission_a_route}")"
wait_port "$(endpoint_host "${mission_b_route}")" "$(endpoint_port "${mission_b_route}")"
wait_http_health "http://$(endpoint_host "${mission_a_http}"):$(endpoint_port "${mission_a_http}")"
wait_http_health "http://$(endpoint_host "${mission_b_http}"):$(endpoint_port "${mission_b_http}")"

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.gamequest.apiName=api-a" "$(app_bin Server/GameApi GameApi)" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.gamequest.apiName=api-b" "$(app_bin Server/GameApi GameApi)" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${api_a_stream}")" "$(endpoint_port "${api_a_stream}")"
wait_port "$(endpoint_host "${api_b_stream}")" "$(endpoint_port "${api_b_stream}")"
wait_http_health "http://$(endpoint_host "${api_a_http}"):$(endpoint_port "${api_a_http}")"
wait_http_health "http://$(endpoint_host "${api_b_http}"):$(endpoint_port "${api_b_http}")"

echo "topology=ready"
JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1
cat "${log_dir}/client.log"

grep -q "gamequest-server-evidence=completed" "${log_dir}/client.log"
grep -q "gamequest=completed" "${log_dir}/client.log"

echo "gamequest kotlin full client/server self-check completed"
