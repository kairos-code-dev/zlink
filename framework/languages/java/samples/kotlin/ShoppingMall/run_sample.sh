#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

source "../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

pids=()
redis_container_id=""
log_dir="build/sample-logs"
store_dir="build/sample-store"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-$(pwd)/logs}"
mkdir -p "${log_dir}" "${store_dir}" "${SHOPPINGMALL_LOG_DIR}"
rm -f "${log_dir}"/*.log
rm -f "${SHOPPINGMALL_LOG_DIR}"/*.log
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

trap cleanup EXIT

reserve_ports() {
  python3 - <<'PY'
import random
import socket
import sys
reserved = []
try:
    chosen = set()
    attempts = 0
    while len(reserved) < 4 and attempts < 1000:
        attempts += 1
        host = "127.0.0.1"
        port = random.randint(48000, 60999)
        key = (host, port)
        if key in chosen:
            continue
        sockets = []
        try:
            sock4 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock4.bind((host, port))
            sockets.append(sock4)
        except OSError:
            for sock in sockets:
                sock.close()
            continue
        chosen.add(key)
        reserved.append((host, port, sockets))
    if len(reserved) < 4:
        print("unable to reserve local TCP ports", file=sys.stderr)
        sys.exit(1)
    print(" ".join(f"{host}:{port}" for host, port, _ in reserved))
finally:
    for _, _, sockets in reserved:
        for sock in sockets:
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
      :zlink-framework-spring-boot-starter:jar \
      :zlink-framework-kotlin:jar \
      :zlink-framework-locations-redis:jar \
      --quiet
  )
}

read -r -a reserved_endpoints < <(reserve_ports)
if [[ "${#reserved_endpoints[@]}" -ne 4 ]]; then
  echo "Failed to reserve sample ports." >&2
  exit 1
fi
commerce_a="${reserved_endpoints[0]}"
commerce_b="${reserved_endpoints[1]}"
workflow_a="${reserved_endpoints[2]}"
workflow_b="${reserved_endpoints[3]}"
commerce_a_host="${commerce_a%:*}"; commerce_a_port="${commerce_a##*:}"
commerce_b_host="${commerce_b%:*}"; commerce_b_port="${commerce_b##*:}"
workflow_a_host="${workflow_a%:*}"; workflow_a_port="${workflow_a##*:}"
workflow_b_host="${workflow_b%:*}"; workflow_b_port="${workflow_b##*:}"

shoppingmall_redis_key_prefix="${SHOPPINGMALL_REDIS_KEY_PREFIX:-shoppingmall:kotlin:${RANDOM}:$$:}"
zlink_redis_start_scoped_assign redis_container_id redis_port \
  "zlink-redis-kotlin-sample-shoppingmall" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
SHOPPINGMALL_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
wait_port "${SHOPPINGMALL_REDIS_ENDPOINT%:*}" "${SHOPPINGMALL_REDIS_ENDPOINT##*:}"

prefix="zlink.samples.shoppingmall"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} \
-D${prefix}.commerceApiAEndpoint=tcp://${commerce_a_host}:${commerce_a_port} \
-D${prefix}.commerceApiBEndpoint=tcp://${commerce_b_host}:${commerce_b_port} \
-D${prefix}.workflowAEndpoint=tcp://${workflow_a_host}:${workflow_a_port} \
-D${prefix}.workflowBEndpoint=tcp://${workflow_b_host}:${workflow_b_port} \
-D${prefix}.redisEndpoint=${SHOPPINGMALL_REDIS_ENDPOINT} \
-D${prefix}.redisKeyPrefix=${shoppingmall_redis_key_prefix} \
-D${prefix}.storeDir=${PWD}/${store_dir}"

build_framework_jars
gradle_run \
  :Server:OrderWorkflow:installDist \
  :Server:CommerceApi:installDist \
  :Client:installDist

"$(app_bin Server/OrderWorkflow OrderWorkflow)" --instance workflow-a >"${log_dir}/workflow-a.log" 2>&1 &
pids+=("$!")
wait_port "${workflow_a_host}" "${workflow_a_port}"

"$(app_bin Server/OrderWorkflow OrderWorkflow)" --instance workflow-b >"${log_dir}/workflow-b.log" 2>&1 &
pids+=("$!")
wait_port "${workflow_b_host}" "${workflow_b_port}"

"$(app_bin Server/CommerceApi CommerceApi)" --instance api-a >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
wait_port "${commerce_a_host}" "${commerce_a_port}"

"$(app_bin Server/CommerceApi CommerceApi)" --instance api-b >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_port "${commerce_b_host}" "${commerce_b_port}"

"$(app_bin Client Client)" >"${log_dir}/client.log" 2>&1

grep -q "shoppingmall order: started" "${log_dir}/workflow-a.log"
grep -q "shoppingmall order: started" "${log_dir}/workflow-b.log"
grep -q "shoppingmall evidence:" "${log_dir}/api-a.log"
grep -Rq "message flow" "${SHOPPINGMALL_LOG_DIR}"
echo "shoppingmall-server-evidence=completed"
