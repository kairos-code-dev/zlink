#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
redis_container_id=""
role_pattern='systems\.zlink\.samples\.kotlin\.deliverydispatch\.(server\.(tracking|customergateway|couriersession|couriergateway|courierspotnode|dispatch)\.ProgramKt|client\.ProgramKt)'
log_dir="build/sample-logs"
state_dir="$(pwd)/build/sample-state"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-$(pwd)/logs}"
export ZLINK_JAVA_STREAM_TRACE="${ZLINK_JAVA_STREAM_TRACE:-1}"
mkdir -p "${log_dir}" "${DELIVERYDISPATCH_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${DELIVERYDISPATCH_LOG_DIR}"/*.log
rm -rf "${state_dir}"
mkdir -p "${state_dir}"

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
  local base=$((20000 + ((RANDOM + $$) % 1000) * 15 % 9000))
  local endpoints=()
  for offset in $(seq 0 14); do
    endpoints+=("127.0.0.1:$((base + offset))")
  done
  echo "${endpoints[*]}"
}

gradle_run() {
  ../../gradlew -Pzlink.useLocalBindings=true --settings-file standalone.settings.gradle.kts --no-daemon "$@" --quiet
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

read -r tracking customer_stream courier_stream courier_gateway dispatch_http customer_route customer_spot customer_router courier_node1_route courier_node2_route courier_node1_spot courier_node2_spot courier_node1_router courier_node2_router courier_session_router < <(reserve_ports)

endpoint_host() { echo "${1%:*}"; }
endpoint_port() { echo "${1##*:}"; }

common_java_options="${JAVA_TOOL_OPTIONS:-}"
common_java_options+=" -Dzlink.samples.deliverydispatch.stateDir=${state_dir}"
common_java_options+=" -Dzlink.samples.deliverydispatch.trackingChannelEndpoint=tcp://$(endpoint_host "${tracking}"):$(endpoint_port "${tracking}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerStreamEndpoint=tcp://$(endpoint_host "${customer_stream}"):$(endpoint_port "${customer_stream}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierStreamEndpoint=tcp://$(endpoint_host "${courier_stream}"):$(endpoint_port "${courier_stream}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierGatewayChannelEndpoint=tcp://$(endpoint_host "${courier_gateway}"):$(endpoint_port "${courier_gateway}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.dispatchHttpEndpoint=http://$(endpoint_host "${dispatch_http}"):$(endpoint_port "${dispatch_http}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerRouteEndpoint=tcp://$(endpoint_host "${customer_route}"):$(endpoint_port "${customer_route}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerSpotEndpoint=tcp://$(endpoint_host "${customer_spot}"):$(endpoint_port "${customer_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerSpotRouterEndpoint=tcp://$(endpoint_host "${customer_router}"):$(endpoint_port "${customer_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode1RouteEndpoint=tcp://$(endpoint_host "${courier_node1_route}"):$(endpoint_port "${courier_node1_route}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode2RouteEndpoint=tcp://$(endpoint_host "${courier_node2_route}"):$(endpoint_port "${courier_node2_route}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode1SpotEndpoint=tcp://$(endpoint_host "${courier_node1_spot}"):$(endpoint_port "${courier_node1_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode2SpotEndpoint=tcp://$(endpoint_host "${courier_node2_spot}"):$(endpoint_port "${courier_node2_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode1RouterEndpoint=tcp://$(endpoint_host "${courier_node1_router}"):$(endpoint_port "${courier_node1_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode2RouterEndpoint=tcp://$(endpoint_host "${courier_node2_router}"):$(endpoint_port "${courier_node2_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierSessionSpotRouterEndpoint=tcp://$(endpoint_host "${courier_session_router}"):$(endpoint_port "${courier_session_router}")"

deliverydispatch_redis_key_prefix="${DELIVERYDISPATCH_REDIS_KEY_PREFIX:-deliverydispatch:kotlin:${RANDOM}:$$:}"
if [[ -z "${DELIVERYDISPATCH_REDIS_ENDPOINT:-}" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required when DELIVERYDISPATCH_REDIS_ENDPOINT is not set." >&2
    exit 1
  fi
  redis_container_id="$(docker run -d --rm --name "deliverydispatch-kotlin-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
  DELIVERYDISPATCH_REDIS_ENDPOINT="$(docker port "${redis_container_id}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
fi
wait_port "${DELIVERYDISPATCH_REDIS_ENDPOINT%:*}" "${DELIVERYDISPATCH_REDIS_ENDPOINT##*:}"
common_java_options+=" -Dzlink.samples.deliverydispatch.redisEndpoint=${DELIVERYDISPATCH_REDIS_ENDPOINT}"
common_java_options+=" -Dzlink.samples.deliverydispatch.redisKeyPrefix=${deliverydispatch_redis_key_prefix}"

build_framework_jars
gradle_run \
  :Server:Tracking:installDist \
  :Server:CustomerGateway:installDist \
  :Server:CourierSession:installDist \
  :Server:CourierSpotNode:installDist \
  :Server:CourierGateway:installDist \
  :Server:Dispatch:installDist \
  :Client:installDist

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/Tracking Tracking)" >"${log_dir}/tracking.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${tracking}")" "$(endpoint_port "${tracking}")"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/CustomerGateway CustomerGateway)" >"${log_dir}/customer-gateway.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${customer_route}")" "$(endpoint_port "${customer_route}")"
wait_port "$(endpoint_host "${customer_stream}")" "$(endpoint_port "${customer_stream}")"
wait_port "$(endpoint_host "${customer_router}")" "$(endpoint_port "${customer_router}")"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/CourierSession CourierSession)" >"${log_dir}/courier-session.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${courier_stream}")" "$(endpoint_port "${courier_stream}")"
wait_port "$(endpoint_host "${courier_session_router}")" "$(endpoint_port "${courier_session_router}")"

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.deliverydispatch.courierNode=node1" "$(app_bin Server/CourierSpotNode CourierSpotNode)" >"${log_dir}/courier-node1.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.deliverydispatch.courierNode=node2" "$(app_bin Server/CourierSpotNode CourierSpotNode)" >"${log_dir}/courier-node2.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${courier_node1_route}")" "$(endpoint_port "${courier_node1_route}")"
wait_port "$(endpoint_host "${courier_node2_route}")" "$(endpoint_port "${courier_node2_route}")"
wait_port "$(endpoint_host "${courier_node1_spot}")" "$(endpoint_port "${courier_node1_spot}")"
wait_port "$(endpoint_host "${courier_node2_spot}")" "$(endpoint_port "${courier_node2_spot}")"
wait_port "$(endpoint_host "${courier_node1_router}")" "$(endpoint_port "${courier_node1_router}")"
wait_port "$(endpoint_host "${courier_node2_router}")" "$(endpoint_port "${courier_node2_router}")"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/CourierGateway CourierGateway)" >"${log_dir}/courier-gateway.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${courier_gateway}")" "$(endpoint_port "${courier_gateway}")"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/Dispatch Dispatch)" >"${log_dir}/dispatch.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${dispatch_http}")" "$(endpoint_port "${dispatch_http}")"

sleep 3
echo "topology=ready"
JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Client Client)" --stream-runtime >"${log_dir}/client.log" 2>&1
cat "${log_dir}/client.log"

grep -q "deliverydispatch-reassignment=completed" "${log_dir}/client.log"
grep -q "deliverydispatch-server-evidence=completed" "${log_dir}/client.log"
grep -q "deliverydispatch=completed" "${log_dir}/client.log"

echo "deliverydispatch full client/server self-check completed"
