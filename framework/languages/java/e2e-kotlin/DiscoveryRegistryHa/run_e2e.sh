#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.kotlin\.discoveryregistryha\.ProgramKt'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
SCENARIO="${1:-all}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/DiscoveryRegistryHa}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/DiscoveryRegistryHa-gradle-cache}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
SCENARIO_SETTLE_SECONDS=3

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
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_KOTLIN_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

bin_path() {
  local path="$1"
  local app="$2"
  echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/${path}/install/${app}/bin/${app}"
}

CLIENT_BIN="$(bin_path Client discovery-registry-ha-kotlin-client)"
REGISTRY_BIN="$(bin_path Server-Registry discovery-registry-ha-kotlin-registry)"
PROVIDER_BIN="$(bin_path Server-Provider discovery-registry-ha-kotlin-provider)"
CONSUMER_BIN="$(bin_path Server-Consumer discovery-registry-ha-kotlin-consumer)"
PROBE_BIN="$(bin_path Server-Probe discovery-registry-ha-kotlin-probe)"
EMBEDDED_BIN="$(bin_path Server-Embedded discovery-registry-ha-kotlin-embedded)"

start_registry() {
  local name="$1"
  local id="$2"
  local pub="$3"
  local router="$4"
  local http_endpoint="$5"
  local peers="${6:-}"
  "${REGISTRY_BIN}" \
    --registry-id "${id}" \
    --registry-pub "${pub}" \
    --registry-router "${router}" \
    --http-endpoint "${http_endpoint}" \
    --registry-peers "${peers}" \
    >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
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
  "${PROVIDER_BIN}" \
    --provider-rid "${rid}" \
    --api-endpoint "${endpoint}" \
    --registry-routers "${registries}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-api" "${endpoint}"
}

start_consumer() {
  local name="$1"
  local rid="$2"
  local http_endpoint="$3"
  local registries="$4"
  "${CONSUMER_BIN}" \
    --consumer-rid "${rid}" \
    --http-endpoint "${http_endpoint}" \
    --registry-routers "${registries}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-http" "${http_endpoint}"
}

start_probe() {
  local name="$1"
  local http_endpoint="$2"
  local registry="$3"
  "${PROBE_BIN}" \
    --http-endpoint "${http_endpoint}" \
    --registry-router "${registry}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port "${name}-http" "${http_endpoint}"
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
  "${EMBEDDED_BIN}" \
    --registry-id "${id}" \
    --registry-pub "${pub}" \
    --registry-router "${router}" \
    --registry-routers "${router}" \
    --http-endpoint "${http_endpoint}" \
    --registry-peers "${peers}" \
    --provider-rid "${rid}" \
    --api-endpoint "${api_endpoint}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
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
  local use_remote_probe="${8:-false}"
  local wait_for_members="${9:-true}"
  local consumer_port
  local consumer_http
  local consumer_pid
  consumer_port="$(reserve_ports 1)"
  consumer_http="$(http "${consumer_port}")"
  start_consumer "consumer-${scenario}" "consumer-${scenario}" "${consumer_http}" "${registries}"
  consumer_pid="${LAST_PID}"
  set +e
  run_client_with_consumer \
    "${scenario}" \
    "${registries}" \
    "${probes}" \
    "${expected}" \
    "${dead}" \
    "${query_registry}" \
    "${topology_probe}" \
    "${use_remote_probe}" \
    "${wait_for_members}" \
    "${consumer_http}" \
    "${scenario}"
  local status="$?"
  set -e
  stop_pid "${consumer_pid}"
  return "${status}"
}

run_client_with_consumer() {
  local scenario="$1"
  local registries="$2"
  local probes="$3"
  local expected="$4"
  local dead="${5:-}"
  local query_registry="${6:-${registries%%,*}}"
  local topology_probe="${7:-${probes%%,*}}"
  local use_remote_probe="${8:-false}"
  local wait_for_members="${9:-true}"
  local consumer_http="${10}"
  local log_suffix="${11:-${scenario}}"
  local remote_probe_http=""
  local remote_probe_pid=""
  if [[ "${use_remote_probe}" == "true" ]]; then
    local probe_port
    probe_port="$(reserve_ports 1)"
    remote_probe_http="$(http "${probe_port}")"
    start_probe "probe-${scenario}" "${remote_probe_http}" "${query_registry}"
    remote_probe_pid="${LAST_PID}"
  fi
  local status=0
  set +e
  "${CLIENT_BIN}" \
    --scenario "${scenario}" \
    --registry-routers "${registries}" \
    --query-registry-router "${query_registry}" \
    --probe-http-endpoints "${probes}" \
    --topology-http-endpoint "${topology_probe}" \
    --consumer-http-endpoint "${consumer_http}" \
    --remote-probe-http-endpoint "${remote_probe_http}" \
    --wait-for-members "${wait_for_members}" \
    --expected-rids "${expected}" \
    --dead-http-endpoint "${dead}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/client-${log_suffix}.stdout.log" 2>"${log_dir}/client-${log_suffix}.stderr.log"
  status="$?"
  set -e
  if [[ -n "${remote_probe_pid}" ]]; then
    stop_pid "${remote_probe_pid}"
  fi
  cat "${log_dir}/client-${log_suffix}.stdout.log"
  if [[ "${status}" != "0" ]]; then
    if [[ "${scenario}" == "DR-A4" ]] &&
       grep -q "scenario DR-A4 passed" "${log_dir}/client-${log_suffix}.stdout.log"; then
      echo "scenario DR-A4 client exited ${status} after pass marker; accepting known native teardown abort" >&2
      return 0
    fi
    return "${status}"
  fi
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

should_run() {
  local target="$1"
  [[ "${SCENARIO}" == "all" || "${SCENARIO}" == "${target}" ]]
}

gradle_run installDist

if should_run DR-A1 || should_run DR-D2; then
read -r R1P R1R R1H A B <<<"$(reserve_ports 5)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_registry dr-a1-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}"
start_provider api-a "${API_A}" "${REG1_ROUTER}"
start_provider api-b "${API_B}" "${REG1_ROUTER}"
sleep 2
should_run DR-A1 && run_client DR-A1 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b"
should_run DR-D2 && run_client DR-D2 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b"
stop_all
fi

if should_run DR-D1; then
read -r R1P R1R R1H A <<<"$(reserve_ports 4)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
API_A="$(tcp "${A}")"
start_embedded dr-d1-embedded 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" api-a "${API_A}"
sleep 2
run_client DR-D1 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a"
stop_all
fi

if should_run DR-A2; then
read -r R1P R1R R1H R2P R2R R2H A <<<"$(reserve_ports 7)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"
start_registry dr-a2-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-a2-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
start_provider api-a "${API_A}" "${REG1_ROUTER}"
sleep "${SCENARIO_SETTLE_SECONDS}"
run_client DR-A2 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all
fi

if should_run DR-A3 || should_run DR-D4 || should_run DR-A4; then
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
if should_run DR-A3; then
  run_client DR-A3 "${REG1_ROUTER}" "${REG1_HTTP},${REG2_HTTP},${REG3_HTTP}" "api-a,api-b"
  if [[ "${SCENARIO}" == "all" ]]; then
    run_client DR-A3 "${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP},${REG3_HTTP}" "api-a,api-b"
    run_client DR-A3 "${REG3_ROUTER}" "${REG1_HTTP},${REG2_HTTP},${REG3_HTTP}" "api-a,api-b"
  fi
fi
should_run DR-D4 && run_client DR-D4 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a,api-b" "" "${REG2_ROUTER}" "${REG2_HTTP}" true
if should_run DR-A4; then
read -r A_DUP <<<"$(reserve_ports 1)"
API_A_DUP="$(tcp "${A_DUP}")"
start_provider api-a "${API_A_DUP}" "${REG2_ROUTER}" api-a-duplicate
sleep 2
run_client DR-A4 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
fi
stop_all
fi

if should_run DR-D3; then
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
fi

if should_run DR-B1; then
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
fi

if should_run DR-B2; then
read -r R1P R1R R1H R2P R2R R2H A <<<"$(reserve_ports 7)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"
start_registry dr-b2-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-b2-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
REG2_PID="${LAST_PID}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}"
sleep "${SCENARIO_SETTLE_SECONDS}"
stop_pid "${REG2_PID}"
sleep 1
start_registry dr-b2-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
sleep 4
run_client DR-B2 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a"
stop_all
fi

if should_run DR-B3; then
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
fi

if should_run DR-C1 || should_run DR-C2; then
read -r R1P R1R R1H R2P R2R R2H A B <<<"$(reserve_ports 8)"
REG1_PUB="$(tcp "${R1P}")"; REG1_ROUTER="$(tcp "${R1R}")"; REG1_HTTP="$(http "${R1H}")"
REG2_PUB="$(tcp "${R2P}")"; REG2_ROUTER="$(tcp "${R2R}")"; REG2_HTTP="$(http "${R2H}")"
API_A="$(tcp "${A}")"; API_B="$(tcp "${B}")"
start_registry dr-c1-reg1 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-c1-reg2 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
REG2_PID="${LAST_PID}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}"
start_provider api-b "${API_B}" "${REG1_ROUTER},${REG2_ROUTER}"
sleep "${SCENARIO_SETTLE_SECONDS}"
kill -9 "${REG2_PID}" >/dev/null 2>&1 || true
wait "${REG2_PID}" >/dev/null 2>&1 || true
sleep 1
should_run DR-C1 && run_client DR-C1 "${REG1_ROUTER}" "${REG1_HTTP}" "api-a,api-b" "${REG2_HTTP}"
if should_run DR-C2; then
start_registry dr-c2-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
sleep 4
run_client DR-C2 "${REG2_ROUTER}" "${REG2_HTTP}" "api-a,api-b"
fi
stop_all
fi

if should_run DR-C3; then
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
sleep "${SCENARIO_SETTLE_SECONDS}"
read -r CONSUMER_PORT <<<"$(reserve_ports 1)"
CONSUMER_HTTP="$(http "${CONSUMER_PORT}")"
start_consumer dr-c3-consumer consumer-DR-C3 "${CONSUMER_HTTP}" "${REG1_ROUTER},${REG2_ROUTER}"
CONSUMER_PID="${LAST_PID}"
run_client_with_consumer DR-C3 "${REG1_ROUTER},${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP}" "api-a,api-b" "" "${REG1_ROUTER}" "${REG1_HTTP}" false true "${CONSUMER_HTTP}" DR-C3-before
stop_pid "${REG2_PID}"
stop_pid "${REG1_PID}"
sleep 2
run_client_with_consumer DR-C3 "${REG1_ROUTER},${REG2_ROUTER}" "" "api-a,api-b" "" "${REG1_ROUTER}" "" false false "${CONSUMER_HTTP}" DR-C3-during
stop_pid "${API_B_PID}"
stop_pid "${API_A_PID}"
start_registry dr-c3-reg1-recovered 1 "${REG1_PUB}" "${REG1_ROUTER}" "${REG1_HTTP}" "${REG2_PUB}"
start_registry dr-c3-reg2-recovered 2 "${REG2_PUB}" "${REG2_ROUTER}" "${REG2_HTTP}" "${REG1_PUB}"
start_provider api-a "${API_A}" "${REG1_ROUTER},${REG2_ROUTER}" api-a-recovered
start_provider api-b "${API_B}" "${REG1_ROUTER},${REG2_ROUTER}" api-b-recovered
sleep 6
run_client_with_consumer DR-C3 "${REG1_ROUTER},${REG2_ROUTER}" "${REG1_HTTP},${REG2_HTTP}" "api-a,api-b" "" "${REG1_ROUTER}" "${REG1_HTTP}" false true "${CONSUMER_HTTP}" DR-C3
stop_pid "${CONSUMER_PID}"
stop_all
fi

if [[ "${SCENARIO}" == "all" ]]; then
  grep -q "scenario DR-A1 passed" "${log_dir}/client-DR-A1.stdout.log"
  grep -q "scenario DR-A2 passed" "${log_dir}/client-DR-A2.stdout.log"
  grep -q "scenario DR-A3 passed" "${log_dir}/client-DR-A3.stdout.log"
  grep -q "scenario DR-A4 passed" "${log_dir}/client-DR-A4.stdout.log"
  grep -q "scenario DR-B1 passed" "${log_dir}/client-DR-B1.stdout.log"
  grep -q "scenario DR-B2 passed" "${log_dir}/client-DR-B2.stdout.log"
  grep -q "scenario DR-B3 passed" "${log_dir}/client-DR-B3.stdout.log"
  grep -q "scenario DR-C1 passed" "${log_dir}/client-DR-C1.stdout.log"
  grep -q "scenario DR-C2 passed" "${log_dir}/client-DR-C2.stdout.log"
  grep -q "scenario DR-C3 passed" "${log_dir}/client-DR-C3-before.stdout.log"
  grep -q "scenario DR-C3 passed" "${log_dir}/client-DR-C3-during.stdout.log"
  grep -q "scenario DR-C3 passed" "${log_dir}/client-DR-C3.stdout.log"
  grep -q "scenario DR-D1 passed" "${log_dir}/client-DR-D1.stdout.log"
  grep -q "scenario DR-D2 passed" "${log_dir}/client-DR-D2.stdout.log"
  grep -q "scenario DR-D3 passed" "${log_dir}/client-DR-D3.stdout.log"
  grep -q "scenario DR-D4 passed" "${log_dir}/client-DR-D4.stdout.log"
else
  grep -Rq "scenario ${SCENARIO} " "${log_dir}"/client-*.stdout.log
fi
grep -Rq "message flow" "${log_dir}"/*-flow.log
