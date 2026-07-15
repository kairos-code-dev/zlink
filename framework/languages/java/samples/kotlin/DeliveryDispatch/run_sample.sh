#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

if rg -n 'customer-1' \
    Server/Tracking/src/main/kotlin --glob 'DeliveryStatusChangedHandler.kt'; then
  echo "Tracking must route status by DeliveryStatusChangedReq.customerId" >&2
  exit 1
fi
if ! rg -q 'customerId = request.customerId' \
    Server/Tracking/src/main/kotlin --glob 'DeliveryStatusChangedHandler.kt'; then
  echo "Tracking must preserve the delivery customer id" >&2
  exit 1
fi
if ! rg -q 'waitForSequence<DeliveryStatusNotify>' \
    Client/src/main/kotlin --glob 'Program.kt'; then
  echo "Client must assert notification arrival order with the connector helper" >&2
  exit 1
fi
if ! rg -q 'expectNone<OfferDeliveryNotify>' \
    Client/src/main/kotlin --glob 'Program.kt'; then
  echo "Client must verify that the other courier receives no offer" >&2
  exit 1
fi
if ! rg -q 'ZLinkKotlinStreamAssert\.ensure\(' \
    Client/src/main/kotlin --glob 'Program.kt'; then
  echo "Client must use the connector assertion utility" >&2
  exit 1
fi
if rg -n 'StatusWaits|arrivals|waitStatus\(|waitStatuses\(' \
    Client/src/main/kotlin --glob 'Program.kt'; then
  echo "Client must not rebuild the connector sequence helper locally" >&2
  exit 1
fi
if rg -n 'runScaffold|waitNotifications|readNotifications|--stream-runtime' \
    Client/src/main/kotlin --glob '*.kt'; then
  echo "DeliveryDispatch client must use the stream connector path only" >&2
  exit 1
fi
coroutine_hosts=(
  Server/CourierGateway/src/main/kotlin
  Server/CourierSession/src/main/kotlin
  Server/CourierSpotNode/src/main/kotlin
  Server/CustomerGateway/src/main/kotlin
  Server/Dispatch/src/main/kotlin
  Server/Tracking/src/main/kotlin
)
for host in "${coroutine_hosts[@]}"; do
  if ! rg -q 'useCoroutineHandlers\(Dispatchers\.Default\)' "${host}" --glob '*.kt'; then
    echo "DeliveryDispatch framework host must configure coroutine handlers: ${host}" >&2
    exit 1
  fi
done

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

pids=()
redis_container_id=""
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

trap cleanup EXIT

reserve_ports() {
  local base=$((20000 + ((RANDOM + $$) % 1000) * 15 % 9000))
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
      :zlink-framework-locations-redis:jar \
      :zlink-stream-connector:jar \
      --quiet
  )
}

read -r tracking tracking_spot customer_stream courier_stream courier_gateway dispatch_http dispatch_channel customer_spot customer_router courier_node1_spot courier_node2_spot courier_node1_router courier_node2_router courier_session_router courier_session_spot < <(reserve_ports)

endpoint_host() { echo "${1%:*}"; }
endpoint_port() { echo "${1##*:}"; }

common_java_options="${JAVA_TOOL_OPTIONS:-}"
common_java_options+=" -Dzlink.samples.deliverydispatch.stateDir=${state_dir}"
common_java_options+=" -Dzlink.samples.deliverydispatch.trackingChannelEndpoint=tcp://$(endpoint_host "${tracking}"):$(endpoint_port "${tracking}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.trackingSpotEndpoint=tcp://$(endpoint_host "${tracking_spot}"):$(endpoint_port "${tracking_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerStreamEndpoint=tcp://$(endpoint_host "${customer_stream}"):$(endpoint_port "${customer_stream}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierStreamEndpoint=tcp://$(endpoint_host "${courier_stream}"):$(endpoint_port "${courier_stream}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierGatewayChannelEndpoint=tcp://$(endpoint_host "${courier_gateway}"):$(endpoint_port "${courier_gateway}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.dispatchHttpEndpoint=http://$(endpoint_host "${dispatch_http}"):$(endpoint_port "${dispatch_http}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.dispatchChannelEndpoint=tcp://$(endpoint_host "${dispatch_channel}"):$(endpoint_port "${dispatch_channel}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerSpotEndpoint=tcp://$(endpoint_host "${customer_spot}"):$(endpoint_port "${customer_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.customerSpotRouterEndpoint=tcp://$(endpoint_host "${customer_router}"):$(endpoint_port "${customer_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode1SpotEndpoint=tcp://$(endpoint_host "${courier_node1_spot}"):$(endpoint_port "${courier_node1_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode2SpotEndpoint=tcp://$(endpoint_host "${courier_node2_spot}"):$(endpoint_port "${courier_node2_spot}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode1RouterEndpoint=tcp://$(endpoint_host "${courier_node1_router}"):$(endpoint_port "${courier_node1_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierActorNode2RouterEndpoint=tcp://$(endpoint_host "${courier_node2_router}"):$(endpoint_port "${courier_node2_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierSessionSpotRouterEndpoint=tcp://$(endpoint_host "${courier_session_router}"):$(endpoint_port "${courier_session_router}")"
common_java_options+=" -Dzlink.samples.deliverydispatch.courierSessionSpotEndpoint=tcp://$(endpoint_host "${courier_session_spot}"):$(endpoint_port "${courier_session_spot}")"

deliverydispatch_redis_key_prefix="${DELIVERYDISPATCH_REDIS_KEY_PREFIX:-deliverydispatch:kotlin:${RANDOM}:$$:}"
zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-kotlin-sample-deliverydispatch" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
DELIVERYDISPATCH_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
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
wait_port "$(endpoint_host "${tracking_spot}")" "$(endpoint_port "${tracking_spot}")"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/CustomerGateway CustomerGateway)" >"${log_dir}/customer-gateway.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${customer_stream}")" "$(endpoint_port "${customer_stream}")"
wait_port "$(endpoint_host "${customer_router}")" "$(endpoint_port "${customer_router}")"

JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Server/CourierSession CourierSession)" >"${log_dir}/courier-session.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${courier_stream}")" "$(endpoint_port "${courier_stream}")"
wait_port "$(endpoint_host "${courier_session_router}")" "$(endpoint_port "${courier_session_router}")"
wait_port "$(endpoint_host "${courier_session_spot}")" "$(endpoint_port "${courier_session_spot}")"

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.deliverydispatch.courierNode=node1" "$(app_bin Server/CourierSpotNode CourierSpotNode)" >"${log_dir}/courier-node1.log" 2>&1 &
pids+=("$!")
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.deliverydispatch.courierNode=node2" "$(app_bin Server/CourierSpotNode CourierSpotNode)" >"${log_dir}/courier-node2.log" 2>&1 &
pids+=("$!")
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

echo "topology=ready"
JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1
cat "${log_dir}/client.log"

grep -q "deliverydispatch-reassignment=completed" "${log_dir}/client.log"
grep -q "deliverydispatch-server-evidence=completed" "${log_dir}/client.log"
grep -q "deliverydispatch=completed" "${log_dir}/client.log"

echo "deliverydispatch full client/server self-check completed"
