#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="${ROOT_DIR}/logs/${RUN_ID}"
SCENARIO="${1:-all}"
ROUTE_SETTLE_SECONDS=5
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
HTTP_PROBE_TIMEOUT_SECONDS=3
mkdir -p "${LOG_DIR}"
echo "log_dir=${LOG_DIR}"

repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi

export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/RegistryMessaging}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/RegistryMessaging-gradle-cache}"

pids=()
role_pattern='systems\.zlink\.e2e\.kotlin\.registrymessaging\.(client|consumer|provider|registry|workflow)\.ProgramKt'

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for log in "${LOG_DIR}"/*.log; do
    [[ -f "${log}" ]] || continue
    echo "===== ${log} =====" >&2
    tail -n 200 "${log}" >&2 || true
  done
}

cleanup() {
  local status="$?"
  set +e
  print_logs "${status}"
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill "${pids[$i]}" >/dev/null 2>&1 || true
  done
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill "${pid}" >/dev/null 2>&1 || true
  done
  sleep 0.5
  for ((i=${#pids[@]}-1; i>=0; i--)); do
    kill -9 "${pids[$i]}" >/dev/null 2>&1 || true
  done
  wait >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

wait_health() {
  local url="$1"
  local name="$2"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if curl --max-time "${HTTP_PROBE_TIMEOUT_SECONDS}" -fsS "${url}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for ${name} at ${url}" >&2
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

CLIENT_BIN="$(bin_path Client registry-messaging-kotlin-client)"
CONSUMER_BIN="$(bin_path Server-Consumer registry-messaging-kotlin-consumer)"
PROVIDER_BIN="$(bin_path Server-Provider registry-messaging-kotlin-provider)"
REGISTRY_BIN="$(bin_path Server-Registry registry-messaging-kotlin-registry)"
WORKFLOW_BIN="$(bin_path Server-Workflow registry-messaging-kotlin-workflow)"

start_server() {
  local name="$1"
  local binary="$2"
  shift 2
  "${binary}" "$@" >"${LOG_DIR}/${name}.stdout.log" 2>"${LOG_DIR}/${name}.stderr.log" &
  pids+=("$!")
}

REG_HTTP_PORT="$(pick_port)"
PROVIDER_A_HTTP_PORT="$(pick_port)"
PROVIDER_B_HTTP_PORT="$(pick_port)"
WORKFLOW_HTTP_PORT="$(pick_port)"
CONSUMER_HTTP_PORT="$(pick_port)"
SINGLE_CONSUMER_HTTP_PORT="$(pick_port)"
DISCOVERY_CONSUMER_HTTP_PORT="$(pick_port)"
BACKPRESSURE_CONSUMER_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"
WORKFLOW_PORT="$(pick_port)"
ROUTE_A_PORT="$(pick_port)"
ROUTE_B_PORT="$(pick_port)"
CLIENT_ROUTE_PORT="$(pick_port)"

REG_PUB="tcp://127.0.0.1:${REG_PUB_PORT}"
REG_ROUTER="tcp://127.0.0.1:${REG_ROUTER_PORT}"
API_A="tcp://127.0.0.1:${API_A_PORT}"
API_B="tcp://127.0.0.1:${API_B_PORT}"
WORKFLOW="tcp://127.0.0.1:${WORKFLOW_PORT}"
ROUTE_A="tcp://127.0.0.1:${ROUTE_A_PORT}"
ROUTE_B="tcp://127.0.0.1:${ROUTE_B_PORT}"

gradle_run installDist

start_server registry "${REGISTRY_BIN}" \
  --rid registry \
  --http-url "http://127.0.0.1:${REG_HTTP_PORT}" \
  --registry-pub-endpoint "${REG_PUB}" \
  --registry-router-endpoint "${REG_ROUTER}" \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${REG_HTTP_PORT}" registry

start_server api-a "${PROVIDER_BIN}" \
  --rid api-a \
  --http-url "http://127.0.0.1:${PROVIDER_A_HTTP_PORT}" \
  --registry-router-endpoint "${REG_ROUTER}" \
  --channel-endpoint "${API_A}" \
  --manual-client-endpoint "${API_A}" \
  --route-endpoint "${ROUTE_A}" \
  --route-peer "${ROUTE_B}" \
  --evidence-file "${LOG_DIR}/api-a.evidence.log" \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${PROVIDER_A_HTTP_PORT}" api-a

start_server api-b "${PROVIDER_BIN}" \
  --rid api-b \
  --http-url "http://127.0.0.1:${PROVIDER_B_HTTP_PORT}" \
  --registry-router-endpoint "${REG_ROUTER}" \
  --channel-endpoint "${API_B}" \
  --manual-client-endpoint "${API_B}" \
  --route-endpoint "${ROUTE_B}" \
  --route-peer "${ROUTE_A}" \
  --evidence-file "${LOG_DIR}/api-b.evidence.log" \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${PROVIDER_B_HTTP_PORT}" api-b

start_server workflow-a "${WORKFLOW_BIN}" \
  --rid workflow-a \
  --http-url "http://127.0.0.1:${WORKFLOW_HTTP_PORT}" \
  --registry-router-endpoint "${REG_ROUTER}" \
  --workflow-endpoint "${WORKFLOW}" \
  --evidence-file "${LOG_DIR}/workflow-a.evidence.log" \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${WORKFLOW_HTTP_PORT}" workflow-a

start_server direct-consumer "${CONSUMER_BIN}" \
  --http-url "http://127.0.0.1:${CONSUMER_HTTP_PORT}" \
  --provider-endpoint "${API_A}" \
  --provider-endpoint "${API_B}" \
  --trace-label direct-consumer \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${CONSUMER_HTTP_PORT}" direct-consumer

start_server single-consumer "${CONSUMER_BIN}" \
  --http-url "http://127.0.0.1:${SINGLE_CONSUMER_HTTP_PORT}" \
  --provider-endpoint "${API_A}" \
  --trace-label single-consumer \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${SINGLE_CONSUMER_HTTP_PORT}" single-consumer

start_server discovery-consumer "${CONSUMER_BIN}" \
  --http-url "http://127.0.0.1:${DISCOVERY_CONSUMER_HTTP_PORT}" \
  --registry-router-endpoint "${REG_ROUTER}" \
  --trace-label discovery-consumer \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${DISCOVERY_CONSUMER_HTTP_PORT}" discovery-consumer

start_server backpressure-consumer "${CONSUMER_BIN}" \
  --http-url "http://127.0.0.1:${BACKPRESSURE_CONSUMER_HTTP_PORT}" \
  --provider-endpoint "${API_A}" \
  --trace-label backpressure-consumer \
  --log-dir "${LOG_DIR}"
wait_health "http://127.0.0.1:${BACKPRESSURE_CONSUMER_HTTP_PORT}" backpressure-consumer

sleep "${ROUTE_SETTLE_SECONDS}"

"${CLIENT_BIN}" \
  --registry-url "http://127.0.0.1:${REG_HTTP_PORT}" \
  --provider-a-url "http://127.0.0.1:${PROVIDER_A_HTTP_PORT}" \
  --provider-b-url "http://127.0.0.1:${PROVIDER_B_HTTP_PORT}" \
  --workflow-url "http://127.0.0.1:${WORKFLOW_HTTP_PORT}" \
  --direct-consumer-url "http://127.0.0.1:${CONSUMER_HTTP_PORT}" \
  --single-consumer-url "http://127.0.0.1:${SINGLE_CONSUMER_HTTP_PORT}" \
  --discovery-consumer-url "http://127.0.0.1:${DISCOVERY_CONSUMER_HTTP_PORT}" \
  --backpressure-consumer-url "http://127.0.0.1:${BACKPRESSURE_CONSUMER_HTTP_PORT}" \
  --registry-bin "${REGISTRY_BIN}" \
  --provider-bin "${PROVIDER_BIN}" \
  --log-dir "${LOG_DIR}" \
  --scenario "${SCENARIO}" \
  >"${LOG_DIR}/client.stdout.log" 2>"${LOG_DIR}/client.stderr.log"

cat "${LOG_DIR}/client.stdout.log"
