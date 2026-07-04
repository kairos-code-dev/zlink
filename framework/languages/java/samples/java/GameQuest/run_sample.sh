#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
redis_container_id=""
role_pattern='systems\.zlink\.samples\.gamequest\.(server\.(gameapi|questmission)\.Program|client\.Program)'
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
  if [[ -n "${redis_container_id}" ]]; then
    docker rm -f "${redis_container_id}" >/dev/null 2>&1 || true
  fi
  for pid in "${pids[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  exit "${status}"
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

gamequest_redis_key_prefix="${GAMEQUEST_REDIS_KEY_PREFIX:-gamequest:java:${RANDOM}:$$:}"
if [[ -z "${GAMEQUEST_REDIS_ENDPOINT:-}" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required when GAMEQUEST_REDIS_ENDPOINT is not set." >&2
    exit 1
  fi
  redis_container_id="$(docker run -d --rm --name "gamequest-java-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
  GAMEQUEST_REDIS_ENDPOINT="$(docker port "${redis_container_id}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
fi
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
wait_port "$(endpoint_host "${mission_a_http}")" "$(endpoint_port "${mission_a_http}")"
wait_port "$(endpoint_host "${mission_b_http}")" "$(endpoint_port "${mission_b_http}")"

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.gamequest.apiName=api-a" "$(app_bin Server/GameApi GameApi)" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.gamequest.apiName=api-b" "$(app_bin Server/GameApi GameApi)" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${api_a_stream}")" "$(endpoint_port "${api_a_stream}")"
wait_port "$(endpoint_host "${api_b_stream}")" "$(endpoint_port "${api_b_stream}")"
wait_port "$(endpoint_host "${api_a_http}")" "$(endpoint_port "${api_a_http}")"
wait_port "$(endpoint_host "${api_b_http}")" "$(endpoint_port "${api_b_http}")"

sleep 3
echo "topology=ready"
JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1
cat "${log_dir}/client.log"

grep -q "gamequest-server-evidence=completed" "${log_dir}/client.log"
grep -q "gamequest=completed" "${log_dir}/client.log"

echo "gamequest full client/server self-check completed"
