#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/e2e-redis-common.sh"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/start-order-common.sh"

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
REDIS_CONTAINER=""
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
SCENARIO="${1:-all}"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"
echo "start_order=${E2E_START_ORDER}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/RegistryMessaging}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/RegistryMessaging-gradle-cache}"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=""
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:registry-messaging:${run_id}}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_SECONDS=5
if [[ "${LOCAL_READINESS_TIMEOUT_SECONDS}" != 3 \
   || "${LOCAL_READINESS_ATTEMPTS}" != 30 \
   || "${ROUTE_SETTLE_SECONDS:-}" != 5 ]]; then
  echo "RegistryMessaging must use 3s readiness and 5s route settle limits" >&2
  exit 1
fi
if rg -n 'java\.net\.http\.HttpClient|HttpClient\.new' \
    "$(pwd)/Client/src/main/java" --glob '*.java'; then
  echo "RegistryMessaging client must use ZLinkHttpClient" >&2
  exit 1
fi

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
  sleep 0.5
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    local pid="${pids[$i]}"
    for child in $(descendants "${pid}"); do
      kill -9 "${child}" >/dev/null 2>&1 || true
    done
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
  if [[ -n "${REDIS_CONTAINER}" ]]; then
    docker rm -fv "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  fi
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

reserve_ports() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(12):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports))
finally:
    for sock in sockets:
        sock.close()
PY
}

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
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

wait_health() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if python3 - "http://127.0.0.1:${port}/health" <<'PY'
import sys
import urllib.request

try:
    with urllib.request.urlopen(sys.argv[1], timeout=0.1) as response:
        sys.exit(0 if 200 <= response.status < 300 else 1)
except Exception:
    sys.exit(1)
PY
    then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} health at ${endpoint}" >&2
  return 1
}

start_redis_container() {
  local redis_port
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required for ${SCENARIO}; it provisions a dedicated Redis location store." >&2
    exit 1
  fi
  zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
    "zlink-redis-java-e2e" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="127.0.0.1:${redis_port}"
  export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

install_dist() {
  if [[ "${SCENARIO}" == "all" || "${SCENARIO}" == "RM-A6" ]]; then
    gradle_run \
      :Client:installDist \
      :Server:Provider:installDist \
      :Server:Workflow:installDist \
      :Server:Consumer:installDist
  else
    gradle_run \
      :Client:installDist \
      :Server:Provider:installDist \
      :Server:Consumer:installDist
  fi
}


client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/registry-messaging-client/bin/registry-messaging-client"
}

provider_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Provider/install/registry-messaging-provider/bin/registry-messaging-provider"
}

workflow_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Workflow/install/registry-messaging-workflow/bin/registry-messaging-workflow"
}

consumer_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Consumer/install/registry-messaging-consumer/bin/registry-messaging-consumer"
}

start_provider() {
  local rid="$1"
  local api="$2"
  local route="$3"
  local workflow="$4"
  local instance="${5:-$rid}"
  local weight="${6:-}"
  local http_port="${7:?http port is required}"
  local binary
  if [[ -n "${workflow}" && -z "${api}" && -z "${route}" ]]; then
    binary="$(workflow_bin)"
  else
    binary="$(provider_bin)"
  fi
  ZLINK_JAVA_E2E_PROVIDER_RID="${rid}" \
  ZLINK_JAVA_E2E_PROVIDER_INSTANCE="${instance}" \
  ZLINK_JAVA_E2E_API_WEIGHT="${weight}" \
  ZLINK_JAVA_E2E_API_ENDPOINT="${api}" \
  ZLINK_JAVA_E2E_API_MANUAL_ENDPOINT="${API_A}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_JAVA_E2E_ROUTE_PEERS="${ROUTE_B}" \
  ZLINK_JAVA_E2E_WORKFLOW_ENDPOINT="${workflow}" \
  ZLINK_JAVA_E2E_HTTP_PORT="$(port_of "${http_port}")" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "${binary}" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  [[ -z "${api}" ]] || wait_port "${rid}-api" "${api}"
  [[ -z "${route}" ]] || wait_port "${rid}-route" "${route}"
  [[ -z "${workflow}" ]] || wait_port "${rid}-workflow" "${workflow}"
  wait_health "${rid}" "${http_port}"
}

start_consumer() {
  local name="$1"
  local mode="$2"
  local http_port="$3"
  local endpoints="${4:-}"
  ZLINK_JAVA_E2E_CONSUMER_NAME="${name}" \
  ZLINK_JAVA_E2E_CONSUMER_MODE="${mode}" \
  ZLINK_JAVA_E2E_PROVIDER_ENDPOINTS="${endpoints}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_HTTP_PORT="$(port_of "${http_port}")" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(consumer_bin)" >"${log_dir}/${name}.stdout.log" 2>"${log_dir}/${name}.stderr.log" &
  pids+=("$!")
  wait_health "${name}" "${http_port}"
}

stop_pid() {
  local pid="$1"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    for _ in $(seq 1 50); do
      if ! kill -0 "${pid}" >/dev/null 2>&1; then
        wait "${pid}" >/dev/null 2>&1 || true
        return
      fi
      sleep 0.1
    done
    for child in $(descendants "${pid}"); do
      kill -9 "${child}" >/dev/null 2>&1 || true
    done
    kill -9 "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

run_client() {
  local scenario="$1"
  local suffix="$2"
  shift 2
  ZLINK_JAVA_E2E_SCENARIO="${scenario}" \
  ZLINK_JAVA_E2E_PROVIDER_A_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_API_A}")" \
  ZLINK_JAVA_E2E_PROVIDER_B_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_API_B}")" \
  ZLINK_JAVA_E2E_WORKFLOW_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_WORKFLOW}")" \
  ZLINK_JAVA_E2E_DISCOVERY_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_DISCOVERY_CONSUMER}")" \
  ZLINK_JAVA_E2E_DIRECT_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_DIRECT_CONSUMER}")" \
  ZLINK_JAVA_E2E_SINGLE_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_SINGLE_CONSUMER}")" \
  ZLINK_JAVA_E2E_BACKPRESSURE_CONSUMER_HTTP_URL="http://127.0.0.1:$(port_of "${HTTP_BACKPRESSURE_CONSUMER}")" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$@" \
    "$(client_bin)" >"${log_dir}/client-${suffix}.stdout.log" 2>"${log_dir}/client-${suffix}.stderr.log"
}

is_common_scenario() {
  case "$1" in
    all|RM-A1|RM-A2|RM-A6|RM-C1|RM-C2|RM-C3|RM-C4|RM-C5|RM-C8|RM-C9)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

needs_workflow_role() {
  case "$1" in
    all|RM-A6)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

read -r API_A API_B ROUTE_A ROUTE_B WORKFLOW_A HTTP_API_A HTTP_API_B HTTP_WORKFLOW HTTP_DISCOVERY_CONSUMER HTTP_DIRECT_CONSUMER HTTP_SINGLE_CONSUMER HTTP_BACKPRESSURE_CONSUMER <<<"$(reserve_ports)"

start_redis_container
install_dist

if is_common_scenario "${SCENARIO}"; then
  SERVER_ROLES=(api-a api-b)
  if needs_workflow_role "${SCENARIO}"; then
    SERVER_ROLES+=(workflow-a)
  fi
  SERVER_ROLES+=(discovery-consumer direct-consumer single-consumer backpressure-consumer)
  mapfile -t ORDERED_SERVER_ROLES < <(zlink_e2e_order_roles "${SERVER_ROLES[@]}")
  for role in "${ORDERED_SERVER_ROLES[@]}"; do
    case "${role}" in
      api-a)
        start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a "" "${HTTP_API_A}"
        API_A_PID="${LAST_PID}"
        ;;
      api-b)
        start_provider api-b "${API_B}" "${ROUTE_B}" "" api-b "" "${HTTP_API_B}"
        API_B_PID="${LAST_PID}"
        ;;
      workflow-a)
        start_provider workflow-a "" "" "${WORKFLOW_A}" workflow-a "" "${HTTP_WORKFLOW}"
        WORKFLOW_A_PID="${LAST_PID}"
        ;;
      discovery-consumer)
        start_consumer discovery-consumer discovery "${HTTP_DISCOVERY_CONSUMER}"
        ;;
      direct-consumer)
        start_consumer direct-consumer direct "${HTTP_DIRECT_CONSUMER}" "${API_A},${API_B}"
        ;;
      single-consumer)
        start_consumer single-consumer direct "${HTTP_SINGLE_CONSUMER}" "${API_A}"
        ;;
      backpressure-consumer)
        start_consumer backpressure-consumer direct "${HTTP_BACKPRESSURE_CONSUMER}" "${API_A}"
        ;;
    esac
  done
  sleep "${ROUTE_SETTLE_SECONDS}"

  common_client_scenario="${SCENARIO}"
  if [[ "${SCENARIO}" == "all" ]]; then
    common_client_scenario="common"
  fi
  run_client "${common_client_scenario}" "${common_client_scenario}" env
  cat "${log_dir}/client-${common_client_scenario}.stdout.log"

  stop_pid "${API_A_PID}"
  stop_pid "${API_B_PID}"
  if needs_workflow_role "${SCENARIO}"; then
    stop_pid "${WORKFLOW_A_PID}"
  fi
fi

if [[ "${SCENARIO}" == "all" || "${SCENARIO}" == "RM-C7" ]]; then
  start_provider api-a "${API_A}" "${ROUTE_A}" "" api-a 75 "${HTTP_API_A}"
  API_A_PID="${LAST_PID}"
  start_provider api-b "${API_B}" "${ROUTE_B}" "" api-b 25 "${HTTP_API_B}"
  API_B_PID="${LAST_PID}"
  sleep "${ROUTE_SETTLE_SECONDS}"
  weighted_client_scenario="${SCENARIO}"
  if [[ "${SCENARIO}" == "all" ]]; then
    weighted_client_scenario="weighted"
  fi
  run_client "${weighted_client_scenario}" rm-c7 env
  cat "${log_dir}/client-rm-c7.stdout.log"
  stop_pid "${API_A_PID}"
  stop_pid "${API_B_PID}"
fi

if [[ "${SCENARIO}" == "all" || "${SCENARIO}" == "RM-B1" ]]; then
  scale_out_client_scenario="${SCENARIO}"
  if [[ "${SCENARIO}" == "all" ]]; then
    scale_out_client_scenario="scale-out"
  fi
  run_client "${scale_out_client_scenario}" rm-b1 env
  cat "${log_dir}/client-rm-b1.stdout.log"
fi

if [[ "${SCENARIO}" == "all" || "${SCENARIO}" == "RM-B2" ]]; then
  scale_in_client_scenario="${SCENARIO}"
  if [[ "${SCENARIO}" == "all" ]]; then
    scale_in_client_scenario="scale-in"
  fi
  run_client "${scale_in_client_scenario}" rm-b2 env
  cat "${log_dir}/client-rm-b2.stdout.log"
fi

if [[ "${SCENARIO}" == "all" || "${SCENARIO}" == "RM-A4" ]]; then
  failover_client_scenario="${SCENARIO}"
  if [[ "${SCENARIO}" == "all" ]]; then
    failover_client_scenario="failover"
  fi
  run_client "${failover_client_scenario}" rm-a4 env
  cat "${log_dir}/client-rm-a4.stdout.log"
fi

grep -Rq "message flow" "${log_dir}"/*-flow.log
