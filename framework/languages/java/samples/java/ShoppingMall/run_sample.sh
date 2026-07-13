#!/usr/bin/env bash
set -euo pipefail
set +m

cd "$(dirname "${BASH_SOURCE[0]}")"

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

pids=()
redis_container_id=""
log_dir="build/sample-logs"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-$(pwd)/logs}"
mkdir -p "${log_dir}" "${SHOPPINGMALL_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${SHOPPINGMALL_LOG_DIR}"/*.log

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

read -r api_a_http api_b_http workflow_a_channel workflow_b_channel workflow_a_spot workflow_b_spot workflow_a_router workflow_b_router < <(reserve_ports)

endpoint_host() { echo "${1%:*}"; }
endpoint_port() { echo "${1##*:}"; }

shoppingmall_redis_key_prefix="${SHOPPINGMALL_REDIS_KEY_PREFIX:-shoppingmall:java:${RANDOM}:$$:}"
zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-java-sample-shoppingmall" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
SHOPPINGMALL_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
wait_port "${SHOPPINGMALL_REDIS_ENDPOINT%:*}" "${SHOPPINGMALL_REDIS_ENDPOINT##*:}"

common_java_options="${JAVA_TOOL_OPTIONS:-}"
common_java_options+=" -Dzlink.samples.shoppingmall.apiAHttpUrl=http://$(endpoint_host "${api_a_http}"):$(endpoint_port "${api_a_http}")"
common_java_options+=" -Dzlink.samples.shoppingmall.apiBHttpUrl=http://$(endpoint_host "${api_b_http}"):$(endpoint_port "${api_b_http}")"
common_java_options+=" -Dzlink.samples.shoppingmall.workflowAChannelEndpoint=tcp://$(endpoint_host "${workflow_a_channel}"):$(endpoint_port "${workflow_a_channel}")"
common_java_options+=" -Dzlink.samples.shoppingmall.workflowBChannelEndpoint=tcp://$(endpoint_host "${workflow_b_channel}"):$(endpoint_port "${workflow_b_channel}")"
common_java_options+=" -Dzlink.samples.shoppingmall.workflowASpotEndpoint=tcp://$(endpoint_host "${workflow_a_spot}"):$(endpoint_port "${workflow_a_spot}")"
common_java_options+=" -Dzlink.samples.shoppingmall.workflowBSpotEndpoint=tcp://$(endpoint_host "${workflow_b_spot}"):$(endpoint_port "${workflow_b_spot}")"
common_java_options+=" -Dzlink.samples.shoppingmall.workflowASpotRouterEndpoint=tcp://$(endpoint_host "${workflow_a_router}"):$(endpoint_port "${workflow_a_router}")"
common_java_options+=" -Dzlink.samples.shoppingmall.workflowBSpotRouterEndpoint=tcp://$(endpoint_host "${workflow_b_router}"):$(endpoint_port "${workflow_b_router}")"
common_java_options+=" -Dzlink.samples.shoppingmall.redisEndpoint=${SHOPPINGMALL_REDIS_ENDPOINT}"
common_java_options+=" -Dzlink.samples.shoppingmall.redisKeyPrefix=${shoppingmall_redis_key_prefix}"

rm -rf \
  Server/CommerceApi/build/install/CommerceApi \
  Server/OrderWorkflow/build/install/OrderWorkflow \
  Client/build/install/Client

gradle_run \
  :Server:CommerceApi:installDist \
  :Server:OrderWorkflow:installDist \
  :Client:installDist

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.shoppingmall.workflowName=workflow-a" "$(app_bin Server/OrderWorkflow OrderWorkflow)" >"${log_dir}/workflow-a.log" 2>&1 &
pids+=("$!")
disown "$!" 2>/dev/null || true
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.shoppingmall.workflowName=workflow-b" "$(app_bin Server/OrderWorkflow OrderWorkflow)" >"${log_dir}/workflow-b.log" 2>&1 &
pids+=("$!")
disown "$!" 2>/dev/null || true
wait_port "$(endpoint_host "${workflow_a_channel}")" "$(endpoint_port "${workflow_a_channel}")"
wait_port "$(endpoint_host "${workflow_b_channel}")" "$(endpoint_port "${workflow_b_channel}")"
wait_port "$(endpoint_host "${workflow_a_spot}")" "$(endpoint_port "${workflow_a_spot}")"
wait_port "$(endpoint_host "${workflow_b_spot}")" "$(endpoint_port "${workflow_b_spot}")"
wait_port "$(endpoint_host "${workflow_a_router}")" "$(endpoint_port "${workflow_a_router}")"
wait_port "$(endpoint_host "${workflow_b_router}")" "$(endpoint_port "${workflow_b_router}")"

JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.shoppingmall.apiName=api-a" "$(app_bin Server/CommerceApi CommerceApi)" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
disown "$!" 2>/dev/null || true
JAVA_TOOL_OPTIONS="${common_java_options} -Dzlink.samples.shoppingmall.apiName=api-b" "$(app_bin Server/CommerceApi CommerceApi)" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
disown "$!" 2>/dev/null || true
wait_http "http://$(endpoint_host "${api_a_http}"):$(endpoint_port "${api_a_http}")"
wait_http "http://$(endpoint_host "${api_b_http}"):$(endpoint_port "${api_b_http}")"

echo "topology=ready"
JAVA_TOOL_OPTIONS="${common_java_options}" "$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1
cat "${log_dir}/client.log"

grep -q "shoppingmall-server-evidence=completed" "${log_dir}/client.log"
grep -q "shoppingmall=completed" "${log_dir}/client.log"
grep -Rq "message flow" "${SHOPPINGMALL_LOG_DIR}"

echo "shoppingmall full client/server self-check completed"
