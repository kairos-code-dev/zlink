#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.discoveryregistryha\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/DiscoveryRegistryHa}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/DiscoveryRegistryHa-gradle-cache}"

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for log in "${log_dir}"/*.log; do
    [[ -f "${log}" ]] || continue
    echo "===== ${log} =====" >&2
    tail -n 160 "${log}" >&2 || true
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
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

reserve_ports() {
  local count="$1"
  python3 - "${count}" <<'PY'
import socket
import sys
count = int(sys.argv[1])
sockets = []
ports = []
try:
    for _ in range(count):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(str(port) for port in ports))
finally:
    for sock in sockets:
        sock.close()
PY
}

tcp() {
  echo "tcp://127.0.0.1:$1"
}

http() {
  echo "http://127.0.0.1:$1"
}

port_of() {
  echo "${1##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

app_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/install/discovery-registry-ha/bin/discovery-registry-ha"
}

start_registry() {
  local name="$1"
  local id="$2"
  local pub="$3"
  local router="$4"
  local http_endpoint="$5"
  local peers="${6:-}"
  ZLINK_JAVA_E2E_ROLE=registry \
  ZLINK_JAVA_E2E_REGISTRY_ID="${id}" \
  ZLINK_JAVA_E2E_REGISTRY_PUB="${pub}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${router}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http_endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_PEERS="${peers}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-router" "${router}"
  wait_port "${name}-http" "${http_endpoint}"
}

start_provider() {
  local rid="$1"
  local endpoint="$2"
  local registries="$3"
  ZLINK_JAVA_E2E_ROLE=provider \
  ZLINK_JAVA_E2E_PROVIDER_RID="${rid}" \
  ZLINK_JAVA_E2E_API_ENDPOINT="${endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${registries}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  pids+=("$!")
  wait_port "${rid}-api" "${endpoint}"
}

start_embedded() {
  local name="$1"
  local id="$2"
  local pub="$3"
  local router="$4"
  local http_endpoint="$5"
  local rid="$6"
  local api_endpoint="$7"
  local peers="${8:-}"
  ZLINK_JAVA_E2E_ROLE=embedded \
  ZLINK_JAVA_E2E_REGISTRY_ID="${id}" \
  ZLINK_JAVA_E2E_REGISTRY_PUB="${pub}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${router}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${router}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http_endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_PEERS="${peers}" \
  ZLINK_JAVA_E2E_PROVIDER_RID="${rid}" \
  ZLINK_JAVA_E2E_API_ENDPOINT="${api_endpoint}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  pids+=("$!")
  wait_port "${name}-router" "${router}"
  wait_port "${name}-http" "${http_endpoint}"
  wait_port "${rid}-api" "${api_endpoint}"
}

run_client() {
  local scenario="$1"
  local registries="$2"
  local probes="$3"
  local expected="$4"
  local dead="${5:-}"
  local query_registry="${6:-${registries%%,*}}"
  local topology_probe="${7:-${probes%%,*}}"
  ZLINK_JAVA_E2E_ROLE=client \
  ZLINK_JAVA_E2E_SCENARIO="${scenario}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${registries}" \
  ZLINK_JAVA_E2E_QUERY_REGISTRY_ROUTER="${query_registry}" \
  ZLINK_JAVA_E2E_PROBE_HTTP_ENDPOINTS="${probes}" \
  ZLINK_JAVA_E2E_TOPOLOGY_HTTP_ENDPOINT="${topology_probe}" \
  ZLINK_JAVA_E2E_EXPECTED_RIDS="${expected}" \
  ZLINK_JAVA_E2E_DEAD_HTTP_ENDPOINT="${dead}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(app_bin)" >"${log_dir}/client-${scenario}.stdout.log" 2>"${log_dir}/client-${scenario}.stderr.log"
  cat "${log_dir}/client-${scenario}.stdout.log"
}

stop_all() {
  set +e
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill "${pids[$i]}" >/dev/null 2>&1 || true
  done
  wait >/dev/null 2>&1 || true
  pids=()
  set -e
}

stop_pid() {
  local pid="$1"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

gradle_run installDist

read -r R1P R1R R1H A B <<<"$(reserve_ports 5)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_registry dr-a1-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}"
start_provider api-a "${API_A}" "${REG1_ROUTER}"
start_provider api-b "${API_B}" "${REG1_ROUTER}"
sleep 2
run_client DR-A1 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b"
run_client DR-D2 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b"
stop_all

read -r R1P R1R R1H A <<<"$(reserve_ports 4)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
API_A="$(tcp "${A}")"
start_embedded dr-d1-embedded 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" api-a "${API_A}"
sleep 2
run_client DR-D1 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a"
stop_all

read -r R1P R1R R1H R2P R2R R2H A <<<"$(reserve_ports 7)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"
start_registry dr-a2-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-a2-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
start_provider api-a "${API_A}" "${REG1_ROUTER}"
sleep 3
run_client DR-A2 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all

read -r R1P R1R R1H R2P R2R R2H R3P R3R R3H A B <<<"$(reserve_ports 11)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
REG3_PUB="$(tcp "${R3P}")"; REG3_ROUTER="$(tcp "${R3R}")"; REG3_HTTP="$(http "${R3H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_registry dr-a3-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB},${REG3_PUB}"
start_registry dr-a3-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB},${REG3_PUB}"
start_registry dr-a3-reg3 3 "${REG3_PUB}" "${REG3_ROUTER}" "${REG3_HTTP}" "${REG1_PUB},${REG2_PUB}"
start_provider api-a "${API_A}" "${REG1_ROUTER}"
start_provider api-b "${API_B}" "${REG3_ROUTER}"
sleep 4
run_client DR-A3 "${REG1_ROUTER}" "${REG1_HTTP},${REG2_HTTP},${REG3_HTTP}" "api-a,api-b"
run_client DR-A3 "${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP},${REG3_HTTP}" "api-a,api-b"
run_client DR-A3 "${REG3_ROUTER}" "${REG1_HTTP},${REG2_HTTP},${REG3_HTTP}" "api-a,api-b"
run_client DR-D4 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a,api-b" "" "${REG2_ROUTER}" "${REG2_HTTP}"
stop_all

read -r R1P R1R R1H R2P R2R R2H A <<<"$(reserve_ports 7)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"
start_registry dr-b1-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_provider api-a "${API_A}" "${REG1_ROUTER}"
sleep 2
start_registry dr-b1-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
sleep 4
run_client DR-B1 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all

read -r R1P R1R R1H R2P R2R R2H A <<<"$(reserve_ports 7)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"
start_registry dr-b2-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-b2-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
REG2_PID="${LAST_PID}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}"
sleep 3
stop_pid "${REG2_PID}"
sleep 1
start_registry dr-b2-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
sleep 4
run_client DR-B2 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all

read -r R1P R1R R1H R2P R2R R2H A <<<"$(reserve_ports 7)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"
start_registry dr-b3-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-b3-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
REG2_PID="${LAST_PID}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}"
sleep 2
for flap in 1 2; do
  stop_pid "${REG2_PID}"
  sleep 1
  start_registry "dr-b3-reg2-flap-${flap}" 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
  REG2_PID="${LAST_PID}"
  sleep 2
done
run_client DR-B3 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all

read -r R1P R1R R1H R2P R2R R2H A B <<<"$(reserve_ports 8)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_registry dr-c1-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-c1-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
REG2_PID="${LAST_PID}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}"
start_provider api-b "${API_B}" "${REG1_ROUTER},${REG2_ROUTER}"
sleep 3
kill -9 "${REG2_PID}" >/dev/null 2>&1 || true
wait "${REG2_PID}" >/dev/null 2>&1 || true
sleep 1
run_client DR-C1 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b" "${REG2_HTTP}"
start_registry dr-c2-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
sleep 4
run_client DR-C2 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a,api-b"
stop_all

grep -q "scenario DR-A1 passed" "${log_dir}/client-DR-A1.stdout.log"
grep -q "scenario DR-A2 passed" "${log_dir}/client-DR-A2.stdout.log"
grep -q "scenario DR-A3 passed" "${log_dir}/client-DR-A3.stdout.log"
grep -q "scenario DR-B1 passed" "${log_dir}/client-DR-B1.stdout.log"
grep -q "scenario DR-B2 passed" "${log_dir}/client-DR-B2.stdout.log"
grep -q "scenario DR-B3 passed" "${log_dir}/client-DR-B3.stdout.log"
grep -q "scenario DR-C1 passed" "${log_dir}/client-DR-C1.stdout.log"
grep -q "scenario DR-C2 passed" "${log_dir}/client-DR-C2.stdout.log"
grep -q "scenario DR-D1 passed" "${log_dir}/client-DR-D1.stdout.log"
grep -q "scenario DR-D2 passed" "${log_dir}/client-DR-D2.stdout.log"
grep -q "scenario DR-D4 passed" "${log_dir}/client-DR-D4.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
