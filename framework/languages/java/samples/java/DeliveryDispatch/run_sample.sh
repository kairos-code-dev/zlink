#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

if rg -n 'System\.(getProperty|getenv)' Server Client --glob '*.java'; then
  echo "DeliveryDispatch application code must use sample config files" >&2
  exit 1
fi
if rg -n -U '\.enableClient\(\s*[^)\s]|\.connect(?:Router|PeerPub)\(' Server --glob '*.java'; then
  echo "DeliveryDispatch server code must use location-store automatic connections" >&2
  exit 1
fi
if ! rg -q 'waitForSequence\(Messages\.DeliveryStatusNotify\.class\)' \
    Client/src/main/java --glob 'DeliveryDispatchClientScenario.java'; then
  echo "DeliveryDispatch client must use the connector sequence helper" >&2
  exit 1
fi
if ! rg -q 'expectNone\(Messages\.OfferDeliveryNotify\.class\)' \
    Client/src/main/java --glob 'DeliveryDispatchClientScenario.java'; then
  echo "DeliveryDispatch must verify that the other courier receives no offer" >&2
  exit 1
fi
if ! rg -q 'ZLinkStreamAssert\.ensure\(' \
    Client/src/main/java --glob 'DeliveryDispatchClientScenario.java'; then
  echo "DeliveryDispatch must use the connector assertion utility" >&2
  exit 1
fi
if rg -n 'assertStatusOrder|observedStatuses|waitStatuses\(' \
    Client/src/main/java --glob 'DeliveryDispatchClientScenario.java'; then
  echo "DeliveryDispatch client must not rebuild the connector sequence helper locally" >&2
  exit 1
fi

if grep -q 'Server:CourierGateway' standalone.settings.gradle.kts; then
  echo "DeliveryDispatch must not include the dead CourierGateway role" >&2
  exit 1
fi
if grep -R -q '@ZLinkHandlerGroup("customer-route")' Server/CustomerGateway/src/main/java; then
  echo "DeliveryDispatch must not retain the unregistered customer-route handlers" >&2
  exit 1
fi

pids=()
redis_container_id=""
log_dir="build/sample-logs"
flow_log_dir="$(pwd)/logs"
config_dir="$(mktemp -d)"
chmod 0700 "${config_dir}"
mkdir -p "${log_dir}" "${flow_log_dir}"
rm -f "${log_dir}"/*.log
rm -f "${flow_log_dir}"/*.log

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

deliverydispatch_cleanup() {
  local status="$?"
  local cleanup_failed=0
  set +e
  print_logs "${status}"

  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill "${pids[$i]}" >/dev/null 2>&1 || true
  done

  for _ in $(seq 1 300); do
    local running=0
    for pid in "${pids[@]}"; do
      local state
      state="$(ps -o stat= -p "${pid}" 2>/dev/null | tr -d ' ')"
      if [[ -n "${state}" && "${state}" != Z* ]]; then
        running=1
        break
      fi
    done
    [[ "${running}" == "0" ]] && break
    sleep 0.1
  done

  for pid in "${pids[@]}"; do
    local state exit_code
    state="$(ps -o stat= -p "${pid}" 2>/dev/null | tr -d ' ')"
    if [[ -n "${state}" && "${state}" != Z* ]]; then
      kill -9 "${pid}" >/dev/null 2>&1 || true
      cleanup_failed=1
    fi
    wait "${pid}"
    exit_code="$?"
    if [[ "${exit_code}" != "0" && "${exit_code}" != "143" ]]; then
      echo "deliverydispatch cleanup process ${pid} exited with ${exit_code}" >&2
      cleanup_failed=1
    fi
  done

  if [[ -n "${redis_container_id}" ]]; then
    zlink_redis_remove_by_id "${redis_container_id}" || cleanup_failed=1
  fi
  rm -rf "${config_dir}"
  if [[ "${status}" != "0" ]]; then
    exit "${status}"
  fi
  if [[ "${cleanup_failed}" != "0" ]]; then
    exit 1
  fi
}

trap deliverydispatch_cleanup EXIT

reserve_ports() {
  python3 - <<'PY'
import random
import socket
reserved = []
try:
    chosen = set()
    while len(reserved) < 15:
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
      :zlink-framework-core:jar \
      :zlink-framework-spring-boot-starter:jar \
      :zlink-framework-locations-redis:jar \
      :zlink-stream-connector:jar \
      --quiet
  )
}

read -r tracking customer_stream courier_stream dispatch_http dispatch_channel customer_spot customer_router tracking_spot_router tracking_spot_pub courier_node1_spot courier_node2_spot courier_node1_router courier_node2_router courier_session_router courier_session_spot < <(reserve_ports)

endpoint_host() { echo "${1%:*}"; }
endpoint_port() { echo "${1##*:}"; }

deliverydispatch_redis_key_prefix="deliverydispatch:java:${RANDOM}:$$:"
zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-java-sample-deliverydispatch" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
redis_endpoint="127.0.0.1:${redis_port}"
wait_port "${redis_endpoint%:*}" "${redis_endpoint##*:}"
write_config() {
  local path="$1" courier_node="$2"
  cat >"$path" <<EOF
trackingChannelEndpoint=tcp://$(endpoint_host "${tracking}"):$(endpoint_port "${tracking}")
customerStreamEndpoint=tcp://$(endpoint_host "${customer_stream}"):$(endpoint_port "${customer_stream}")
courierStreamEndpoint=tcp://$(endpoint_host "${courier_stream}"):$(endpoint_port "${courier_stream}")
dispatchHttpEndpoint=http://$(endpoint_host "${dispatch_http}"):$(endpoint_port "${dispatch_http}")
dispatchChannelEndpoint=tcp://$(endpoint_host "${dispatch_channel}"):$(endpoint_port "${dispatch_channel}")
trackingSpotEndpoint=tcp://$(endpoint_host "${tracking_spot_router}"):$(endpoint_port "${tracking_spot_router}")
customerSpotEndpoint=tcp://$(endpoint_host "${customer_spot}"):$(endpoint_port "${customer_spot}")
customerSpotRouterEndpoint=tcp://$(endpoint_host "${customer_router}"):$(endpoint_port "${customer_router}")
trackingSpotPubEndpoint=tcp://$(endpoint_host "${tracking_spot_pub}"):$(endpoint_port "${tracking_spot_pub}")
courierActorNode1SpotEndpoint=tcp://$(endpoint_host "${courier_node1_spot}"):$(endpoint_port "${courier_node1_spot}")
courierActorNode2SpotEndpoint=tcp://$(endpoint_host "${courier_node2_spot}"):$(endpoint_port "${courier_node2_spot}")
courierActorNode1RouterEndpoint=tcp://$(endpoint_host "${courier_node1_router}"):$(endpoint_port "${courier_node1_router}")
courierActorNode2RouterEndpoint=tcp://$(endpoint_host "${courier_node2_router}"):$(endpoint_port "${courier_node2_router}")
courierSessionSpotRouterEndpoint=tcp://$(endpoint_host "${courier_session_router}"):$(endpoint_port "${courier_session_router}")
courierSessionSpotEndpoint=tcp://$(endpoint_host "${courier_session_spot}"):$(endpoint_port "${courier_session_spot}")
redisEndpoint=${redis_endpoint}
redisKeyPrefix=${deliverydispatch_redis_key_prefix}
courierNode=${courier_node}
logDirectory=${flow_log_dir}
EOF
  chmod 0600 "$path"
}
tracking_config="${config_dir}/tracking.properties"
customer_gateway_config="${config_dir}/customer-gateway.properties"
courier_session_config="${config_dir}/courier-session.properties"
courier_node1_config="${config_dir}/courier-node1.properties"
courier_node2_config="${config_dir}/courier-node2.properties"
dispatch_config="${config_dir}/dispatch.properties"
client_config="${config_dir}/client.properties"
write_config "$tracking_config" node1
write_config "$customer_gateway_config" node1
write_config "$courier_session_config" node1
write_config "$courier_node1_config" node1
write_config "$courier_node2_config" node2
write_config "$dispatch_config" node1
write_config "$client_config" node1

build_framework_jars
gradle_run \
  :Server:Tracking:installDist \
  :Server:CustomerGateway:installDist \
  :Server:CourierSession:installDist \
  :Server:CourierSpotNode:installDist \
  :Server:Dispatch:installDist \
  :Client:installDist

"$(app_bin Server/Tracking Tracking)" --config "$tracking_config" >"${log_dir}/tracking.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${tracking}")" "$(endpoint_port "${tracking}")"

"$(app_bin Server/CustomerGateway CustomerGateway)" --config "$customer_gateway_config" >"${log_dir}/customer-gateway.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${customer_stream}")" "$(endpoint_port "${customer_stream}")"
wait_port "$(endpoint_host "${customer_router}")" "$(endpoint_port "${customer_router}")"

"$(app_bin Server/CourierSession CourierSession)" --config "$courier_session_config" >"${log_dir}/courier-session.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${courier_stream}")" "$(endpoint_port "${courier_stream}")"
wait_port "$(endpoint_host "${courier_session_router}")" "$(endpoint_port "${courier_session_router}")"
wait_port "$(endpoint_host "${courier_session_spot}")" "$(endpoint_port "${courier_session_spot}")"

"$(app_bin Server/CourierSpotNode CourierSpotNode)" --config "$courier_node1_config" >"${log_dir}/courier-node1.log" 2>&1 &
pids+=("$!")
"$(app_bin Server/CourierSpotNode CourierSpotNode)" --config "$courier_node2_config" >"${log_dir}/courier-node2.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${courier_node1_spot}")" "$(endpoint_port "${courier_node1_spot}")"
wait_port "$(endpoint_host "${courier_node2_spot}")" "$(endpoint_port "${courier_node2_spot}")"
wait_port "$(endpoint_host "${courier_node1_router}")" "$(endpoint_port "${courier_node1_router}")"
wait_port "$(endpoint_host "${courier_node2_router}")" "$(endpoint_port "${courier_node2_router}")"

"$(app_bin Server/Dispatch Dispatch)" --config "$dispatch_config" >"${log_dir}/dispatch.log" 2>&1 &
pids+=("$!")
wait_port "$(endpoint_host "${dispatch_http}")" "$(endpoint_port "${dispatch_http}")"

echo "topology=ready"
"$(app_bin Client Client)" --config "$client_config" >"${log_dir}/client.log" 2>&1
cat "${log_dir}/client.log"

grep -q "deliverydispatch-reassignment=completed" "${log_dir}/client.log"
grep -q "deliverydispatch-server-evidence=completed" "${log_dir}/client.log"
grep -q "deliverydispatch=completed" "${log_dir}/client.log"
for courier_id in courier-a courier-b; do
  if ! grep -q "courier-bind-relayed=${courier_id}" \
      "${log_dir}/courier-node1.log" "${log_dir}/courier-node2.log"; then
    echo "DeliveryDispatch bind did not reach the courier actor: ${courier_id}" >&2
    exit 1
  fi
done

echo "deliverydispatch full client/server self-check completed"
