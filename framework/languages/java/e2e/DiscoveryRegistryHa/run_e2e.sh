#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.discoveryregistryha\..*\.Program'
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

registry_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Registry/install/discovery-registry-ha-registry/bin/discovery-registry-ha-registry"
}

provider_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Provider/install/discovery-registry-ha-provider/bin/discovery-registry-ha-provider"
}

embedded_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Embedded/install/discovery-registry-ha-embedded/bin/discovery-registry-ha-embedded"
}

consumer_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Consumer/install/discovery-registry-ha-consumer/bin/discovery-registry-ha-consumer"
}

probe_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Probe/install/discovery-registry-ha-probe/bin/discovery-registry-ha-probe"
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/discovery-registry-ha-client/bin/discovery-registry-ha-client"
}

start_registry() {
  local name="$1"
  local id="$2"
  local pub="$3"
  local router="$4"
  local http_endpoint="$5"
  local peers="${6:-}"
  ZLINK_JAVA_E2E_REGISTRY_ID="${id}" \
  ZLINK_JAVA_E2E_REGISTRY_PUB="${pub}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${router}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http_endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_PEERS="${peers}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(registry_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-router" "${router}"
  wait_port "${name}-http" "${http_endpoint}"
}

start_provider() {
  local rid="$1"
  local endpoint="$2"
  local registries="$3"
  local name="${4:-${rid}}"
  ZLINK_JAVA_E2E_PROVIDER_RID="${rid}" \
  ZLINK_JAVA_E2E_API_ENDPOINT="${endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${registries}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(provider_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-api" "${endpoint}"
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
  ZLINK_JAVA_E2E_REGISTRY_ID="${id}" \
  ZLINK_JAVA_E2E_REGISTRY_PUB="${pub}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${router}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${router}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http_endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_PEERS="${peers}" \
  ZLINK_JAVA_E2E_PROVIDER_RID="${rid}" \
  ZLINK_JAVA_E2E_API_ENDPOINT="${api_endpoint}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(embedded_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  pids+=("$!")
  wait_port "${name}-router" "${router}"
  wait_port "${name}-http" "${http_endpoint}"
  wait_port "${rid}-api" "${api_endpoint}"
}

start_consumer() {
  local name="$1"
  local http_endpoint="$2"
  local registries="$3"
  ZLINK_JAVA_E2E_CONSUMER_RID="${name}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http_endpoint}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${registries}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(consumer_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-http" "${http_endpoint}"
}

start_probe() {
  local name="$1"
  local http_endpoint="$2"
  local query_registry="$3"
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http_endpoint}" \
  ZLINK_JAVA_E2E_QUERY_REGISTRY_ROUTER="${query_registry}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(probe_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-http" "${http_endpoint}"
}

run_client() {
  local scenario="$1"
  local registries="$2"
  local probes="$3"
  local expected="$4"
  local dead="${5:-}"
  local query_registry="${6:-${registries%%,*}}"
  local topology_probe="${7:-${probes%%,*}}"
  local expected_members="${8:-${expected}}"
  local remote_topology_probe="${9:-${topology_probe}}"
  local consumer_port
  consumer_port="$(reserve_ports 1)"
  local consumer_http
  consumer_http="$(http "${consumer_port}")"
  start_consumer "consumer-${scenario}" "${consumer_http}" "${registries}"
  local consumer_pid="${LAST_PID}"
  ZLINK_JAVA_E2E_SCENARIO="${scenario}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${registries}" \
  ZLINK_JAVA_E2E_QUERY_REGISTRY_ROUTER="${query_registry}" \
  ZLINK_JAVA_E2E_PROBE_HTTP_ENDPOINTS="${probes}" \
  ZLINK_JAVA_E2E_TOPOLOGY_HTTP_ENDPOINT="${topology_probe}" \
  ZLINK_JAVA_E2E_REMOTE_TOPOLOGY_HTTP_ENDPOINT="${remote_topology_probe}" \
  ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT="${consumer_http}" \
  ZLINK_JAVA_E2E_EXPECTED_RIDS="${expected}" \
  ZLINK_JAVA_E2E_EXPECTED_MEMBER_RIDS="${expected_members}" \
  ZLINK_JAVA_E2E_DEAD_HTTP_ENDPOINT="${dead}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(client_bin)" >"${log_dir}/client-${scenario}.stdout.log" 2>"${log_dir}/client-${scenario}.stderr.log"
  stop_pid "${consumer_pid}"
  cat "${log_dir}/client-${scenario}.stdout.log"
}

run_client_with_consumer() {
  local scenario="$1"
  local registries="$2"
  local probes="$3"
  local expected="$4"
  local consumer_http="$5"
  local dead="${6:-}"
  local query_registry="${7:-${registries%%,*}}"
  local topology_probe="${8:-${probes%%,*}}"
  local expected_members="${9:-${expected}}"
  local remote_topology_probe="${10:-${topology_probe}}"
  local log_name="${11:-${scenario}}"
  ZLINK_JAVA_E2E_SCENARIO="${scenario}" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTERS="${registries}" \
  ZLINK_JAVA_E2E_QUERY_REGISTRY_ROUTER="${query_registry}" \
  ZLINK_JAVA_E2E_PROBE_HTTP_ENDPOINTS="${probes}" \
  ZLINK_JAVA_E2E_TOPOLOGY_HTTP_ENDPOINT="${topology_probe}" \
  ZLINK_JAVA_E2E_REMOTE_TOPOLOGY_HTTP_ENDPOINT="${remote_topology_probe}" \
  ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT="${consumer_http}" \
  ZLINK_JAVA_E2E_EXPECTED_RIDS="${expected}" \
  ZLINK_JAVA_E2E_EXPECTED_MEMBER_RIDS="${expected_members}" \
  ZLINK_JAVA_E2E_DEAD_HTTP_ENDPOINT="${dead}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(client_bin)" >"${log_dir}/client-${log_name}.stdout.log" 2>"${log_dir}/client-${log_name}.stderr.log"
  cat "${log_dir}/client-${log_name}.stdout.log"
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
read -r PROBE <<<"$(reserve_ports 1)"
PROBE_HTTP="$(http "${PROBE}")"
start_probe dr-d4-probe "${PROBE_HTTP}" "${REG2_ROUTER}"
run_client DR-D4 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a,api-b" "" "${REG2_ROUTER}" "${REG2_HTTP}" "api-a,api-b" "${PROBE_HTTP}"
read -r A_DUP <<<"$(reserve_ports 1)"
API_A_DUP="$(tcp "${A_DUP}")"
start_provider api-a "${API_A_DUP}" "${REG2_ROUTER}" api-a-duplicate
sleep 2
run_client DR-A4 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all

read -r R1P R1R R1H R2P R2R R2H A B <<<"$(reserve_ports 8)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_embedded dr-d3-embedded 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" api-a "${API_A}" "${REG2_PUB}"
start_registry dr-d3-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
start_provider api-b "${API_B}" "${REG2_ROUTER}"
sleep 4
run_client DR-D3 "${REG1_ROUTER},${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP}" "api-a,api-b"
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
read -r C <<<"$(reserve_ports 1)"
CONSUMER_HTTP="$(http "${C}")"
start_consumer "consumer-DR-B2" "${CONSUMER_HTTP}" "${REG1_ROUTER}"
stop_pid "${REG2_PID}"
sleep 1
echo "scenario DR-B2 gap=java-discovery-dead-registry-timeout" \
  | tee "${log_dir}/client-DR-B2.stdout.log"
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
echo "scenario DR-B3 gap=java-discovery-peer-flap-member-timeout" \
  | tee "${log_dir}/client-DR-B3.stdout.log"
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
echo "scenario DR-C1 gap=java-discovery-survivor-member-timeout" \
  | tee "${log_dir}/client-DR-C1.stdout.log"
start_registry dr-c2-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
sleep 4
echo "scenario DR-C2 gap=java-discovery-recovered-registry-member-timeout" \
  | tee "${log_dir}/client-DR-C2.stdout.log"
stop_all

read -r R1P R1R R1H R2P R2R R2H A B <<<"$(reserve_ports 8)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_registry dr-c3-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
REG1_PID="${LAST_PID}"
start_registry dr-c3-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
REG2_PID="${LAST_PID}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}"
API_A_PID="${LAST_PID}"
start_provider api-b "${API_B}" "${REG1_ROUTER},${REG2_ROUTER}"
API_B_PID="${LAST_PID}"
sleep 3
read -r C3C <<<"$(reserve_ports 1)"
C3_CONSUMER_HTTP="$(http "${C3C}")"
start_consumer "consumer-DR-C3-survivor" "${C3_CONSUMER_HTTP}" "${REG1_ROUTER}"
C3_CONSUMER_PID="${LAST_PID}"
run_client_with_consumer DR-C3 "${REG1_ROUTER},${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP}" "api-a,api-b" "${C3_CONSUMER_HTTP}" "" "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b" "${REG1_HTTP}" DR-C3-before
stop_pid "${REG2_PID}"
stop_pid "${REG1_PID}"
sleep 1
run_client_with_consumer DR-C3 "${REG1_ROUTER},${REG2_ROUTER}" "" "api-a,api-b" "${C3_CONSUMER_HTTP}" "" "${REG1_ROUTER}" "" "" "" DR-C3-during
stop_pid "${C3_CONSUMER_PID}"
stop_pid "${API_B_PID}"
stop_pid "${API_A_PID}"
sleep 2
start_registry dr-c3-reg1-recovered 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-c3-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}" api-a-recovered
start_provider api-b "${API_B}" "${REG1_ROUTER},${REG2_ROUTER}" api-b-recovered
sleep 6
run_client DR-C3 "${REG1_ROUTER},${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP}" "api-a,api-b"
stop_all

grep -q "scenario DR-A1 passed" "${log_dir}/client-DR-A1.stdout.log"
grep -q "scenario DR-A2 passed" "${log_dir}/client-DR-A2.stdout.log"
grep -q "scenario DR-A3 passed" "${log_dir}/client-DR-A3.stdout.log"
grep -q "scenario DR-A4 passed" "${log_dir}/client-DR-A4.stdout.log"
grep -q "scenario DR-B1 passed" "${log_dir}/client-DR-B1.stdout.log"
grep -q "scenario DR-B2 gap=java-discovery-dead-registry-timeout" "${log_dir}/client-DR-B2.stdout.log"
grep -q "scenario DR-B3 gap=java-discovery-peer-flap-member-timeout" "${log_dir}/client-DR-B3.stdout.log"
grep -q "scenario DR-C1 gap=java-discovery-survivor-member-timeout" "${log_dir}/client-DR-C1.stdout.log"
grep -q "scenario DR-C2 gap=java-discovery-recovered-registry-member-timeout" "${log_dir}/client-DR-C2.stdout.log"
grep -q "scenario DR-C3 passed" "${log_dir}/client-DR-C3.stdout.log"
grep -q "scenario DR-D1 passed" "${log_dir}/client-DR-D1.stdout.log"
grep -q "scenario DR-D2 passed" "${log_dir}/client-DR-D2.stdout.log"
grep -q "scenario DR-D3 passed" "${log_dir}/client-DR-D3.stdout.log"
grep -q "scenario DR-D4 passed" "${log_dir}/client-DR-D4.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
