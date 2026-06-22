#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.samples\.kotlin\.gamequest\.(server\.(registry|gameapi|questmission)\.ProgramKt|client\.ProgramKt)'
log_dir="build/sample-logs"
store_dir="build/sample-store"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-$(pwd)/logs}"
mkdir -p "${log_dir}" "${store_dir}" "${GAMEQUEST_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${GAMEQUEST_LOG_DIR}"/*.log
rm -f "${store_dir}"/*

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
  local endpoint="$1"
  local host="${endpoint%:*}"
  local port="${endpoint##*:}"
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
    while len(reserved) < 14:
        host = "127.0.0.1"
        port = random.randint(48000, 60999)
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
  local project_path="$1"
  local app_name="$2"
  printf '%s/build/install/%s/bin/%s' "${project_path}" "${app_name}" "${app_name}"
}

build_framework_jars() {
  (
    cd ../../..
    ./gradlew --no-daemon \
      :zlink-framework-core:jar \
      :zlink-framework-spring-boot-starter:jar \
      :zlink-framework-kotlin:jar \
      --quiet
  )
}

read -r registry_pub registry_router fanout_a fanout_b api_a api_b stream_a stream_b mission_a mission_b spot_a spot_a_router spot_b spot_b_router < <(reserve_ports)

prefix="zlink.samples.gamequest"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} \
-D${prefix}.registryPubEndpoint=tcp://${registry_pub} \
-D${prefix}.registryRouterEndpoint=tcp://${registry_router} \
-D${prefix}.fanoutPublisherAEndpoint=tcp://${fanout_a} \
-D${prefix}.fanoutPublisherBEndpoint=tcp://${fanout_b} \
-D${prefix}.gameApiAActionEndpoint=tcp://${api_a} \
-D${prefix}.gameApiBActionEndpoint=tcp://${api_b} \
-D${prefix}.gameApiAStreamEndpoint=tcp://${stream_a} \
-D${prefix}.gameApiBStreamEndpoint=tcp://${stream_b} \
-D${prefix}.missionAActionEndpoint=tcp://${mission_a} \
-D${prefix}.missionBActionEndpoint=tcp://${mission_b} \
-D${prefix}.missionASpotEndpoint=tcp://${spot_a} \
-D${prefix}.missionASpotRouterEndpoint=tcp://${spot_a_router} \
-D${prefix}.missionBSpotEndpoint=tcp://${spot_b} \
-D${prefix}.missionBSpotRouterEndpoint=tcp://${spot_b_router} \
-D${prefix}.storeDirectory=${PWD}/${store_dir}"

build_framework_jars
gradle_run \
  :Server:Registry:installDist \
  :Server:QuestMission:installDist \
  :Server:GameApi:installDist \
  :Client:installDist

"$(app_bin Server/Registry Registry)" >"${log_dir}/registry.log" 2>&1 &
pids+=("$!")
wait_port "${registry_pub}"
wait_port "${registry_router}"

"$(app_bin Server/QuestMission QuestMission)" --mission mission-a >"${log_dir}/mission-a.log" 2>&1 &
pids+=("$!")
wait_port "${mission_a}"
wait_port "${spot_a}"
wait_port "${spot_a_router}"

"$(app_bin Server/QuestMission QuestMission)" --mission mission-b >"${log_dir}/mission-b.log" 2>&1 &
pids+=("$!")
wait_port "${mission_b}"
wait_port "${spot_b}"
wait_port "${spot_b_router}"

"$(app_bin Server/GameApi GameApi)" --api api-a >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
wait_port "${fanout_a}"
wait_port "${api_a}"
wait_port "${stream_a}"

"$(app_bin Server/GameApi GameApi)" --api api-b >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_port "${fanout_b}"
wait_port "${api_b}"
wait_port "${stream_b}"

"$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1

grep -q "gamequest mission processed" "${log_dir}/mission-a.log"
grep -q "gamequest mission processed" "${log_dir}/mission-b.log"
grep -q "gamequest evidence: passed=true" "${log_dir}/api-a.log" "${log_dir}/api-b.log"
grep -q "gamequest=completed" "${log_dir}/client.log"
grep -Rq "message flow" "${GAMEQUEST_LOG_DIR}"
echo "gamequest-server-evidence=completed"
