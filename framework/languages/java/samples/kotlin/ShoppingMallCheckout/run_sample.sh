#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.samples\.kotlin\.shoppingmallcheckout\.(server\.(registry|commerceapi|orderworkflow)\.ProgramKt|client\.ProgramKt)'
log_dir="build/sample-logs"
store_dir="build/sample-store"
mkdir -p "${log_dir}" "${store_dir}"
rm -f "${log_dir}"/*.log
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
    while len(reserved) < 6:
        host = "127.0.0.1"
        port = random.randint(20000, 32767)
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
    print(" ".join(f"{host}:{port}" for host, port, _ in reserved))
finally:
    for _, _, sockets in reserved:
        for sock in sockets:
            sock.close()
PY
}

gradle_run() {
  ../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon "$@" --quiet
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

read -r registry_pub registry_router commerce_a commerce_b workflow_a workflow_b < <(reserve_ports)
registry_pub_host="${registry_pub%:*}"; registry_pub_port="${registry_pub##*:}"
registry_router_host="${registry_router%:*}"; registry_router_port="${registry_router##*:}"
commerce_a_host="${commerce_a%:*}"; commerce_a_port="${commerce_a##*:}"
commerce_b_host="${commerce_b%:*}"; commerce_b_port="${commerce_b##*:}"
workflow_a_host="${workflow_a%:*}"; workflow_a_port="${workflow_a##*:}"
workflow_b_host="${workflow_b%:*}"; workflow_b_port="${workflow_b##*:}"

prefix="zlink.samples.shoppingmallcheckout"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} \
-D${prefix}.registryPubEndpoint=tcp://${registry_pub_host}:${registry_pub_port} \
-D${prefix}.registryRouterEndpoint=tcp://${registry_router_host}:${registry_router_port} \
-D${prefix}.commerceApiAEndpoint=tcp://${commerce_a_host}:${commerce_a_port} \
-D${prefix}.commerceApiBEndpoint=tcp://${commerce_b_host}:${commerce_b_port} \
-D${prefix}.workflowAEndpoint=tcp://${workflow_a_host}:${workflow_a_port} \
-D${prefix}.workflowBEndpoint=tcp://${workflow_b_host}:${workflow_b_port} \
-D${prefix}.storeDir=${PWD}/${store_dir}"

build_framework_jars
gradle_run classes

gradle_run :Server:Registry:run >"${log_dir}/registry.log" 2>&1 &
pids+=("$!")
wait_port "${registry_pub_host}" "${registry_pub_port}"
wait_port "${registry_router_host}" "${registry_router_port}"

gradle_run :Server:OrderWorkflow:run --args="--instance workflow-a" >"${log_dir}/workflow-a.log" 2>&1 &
pids+=("$!")
wait_port "${workflow_a_host}" "${workflow_a_port}"

gradle_run :Server:OrderWorkflow:run --args="--instance workflow-b" >"${log_dir}/workflow-b.log" 2>&1 &
pids+=("$!")
wait_port "${workflow_b_host}" "${workflow_b_port}"

gradle_run :Server:CommerceApi:run --args="--instance api-a" >"${log_dir}/api-a.log" 2>&1 &
pids+=("$!")
wait_port "${commerce_a_host}" "${commerce_a_port}"

gradle_run :Server:CommerceApi:run --args="--instance api-b" >"${log_dir}/api-b.log" 2>&1 &
pids+=("$!")
wait_port "${commerce_b_host}" "${commerce_b_port}"

gradle_run :Client:run >"${log_dir}/client.log" 2>&1

grep -q "shoppingmall order: started" "${log_dir}/workflow-a.log"
grep -q "shoppingmall order: started" "${log_dir}/workflow-b.log"
grep -q "shoppingmall evidence:" "${log_dir}/api-a.log"
echo "shoppingmall-server-evidence=completed"
