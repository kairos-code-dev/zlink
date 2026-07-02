#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

pids=()
role_pattern='systems\.zlink\.e2e\.kotlin\.pubsub\.(client|publisher|registry|subscriber)\.ProgramKt'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="${ROOT_DIR}/logs/${run_id}"
SCENARIO="${1:-all}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/PubSub}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/PubSub-gradle-cache}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
HTTP_PROBE_TIMEOUT_SECONDS=3
SCENARIO_MARKER_ATTEMPTS=30
SCENARIO_MARKER_INTERVAL=0.1
ROUTE_SETTLE_SECONDS=5

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

wait_marker() {
  local file="$1"
  for _ in $(seq 1 "${SCENARIO_MARKER_ATTEMPTS}"); do
    if [[ -f "${file}" ]]; then
      return 0
    fi
    sleep "${SCENARIO_MARKER_INTERVAL}"
  done
  echo "Timed out waiting for marker ${file}" >&2
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

CLIENT_BIN="$(bin_path Client pub-sub-kotlin-client)"
PUBLISHER_BIN="$(bin_path Server-Publisher pub-sub-kotlin-publisher)"
REGISTRY_BIN="$(bin_path Server-Registry pub-sub-kotlin-registry)"
SUBSCRIBER_BIN="$(bin_path Server-Subscriber pub-sub-kotlin-subscriber)"

start_registry() {
  "${REGISTRY_BIN}" \
    --registry-pub-endpoint "${REGISTRY_PUB}" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --http-endpoint "${REGISTRY_HTTP}" \
    >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
  wait_health "${REGISTRY_HTTP}" registry
}

start_publisher() {
  local suffix="${1:-publisher}"
  "${PUBLISHER_BIN}" \
    --publisher-endpoint "${PUBLISHER_ENDPOINT}" \
    --http-endpoint "${PUBLISHER_HTTP}" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/${suffix}.stdout.log" 2>"${log_dir}/${suffix}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_port publisher "${PUBLISHER_ENDPOINT}"
  wait_health "${PUBLISHER_HTTP}" publisher
}

start_subscriber() {
  local rid="$1"
  local topics="$2"
  local http="$3"
  local delay="${4:-}"
  "${SUBSCRIBER_BIN}" \
    --rid "${rid}" \
    --topics "${topics}" \
    --http-endpoint "${http}" \
    --handler-delay-ms "${delay}" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --log-dir "${log_dir}" \
    >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  LAST_PID="$!"
  pids+=("${LAST_PID}")
  wait_health "${http}" "${rid}"
}

stop_pid() {
  local pid="$1"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
}

run_client_mode() {
  local mode="$1"
  local suffix="$2"
  "${CLIENT_BIN}" \
    --mode "${mode}" \
    --publisher-http "${PUBLISHER_HTTP}" \
    --sub1-http "${SUB1_HTTP}" \
    --sub2-http "${SUB2_HTTP}" \
    --sub3-http "${SUB3_HTTP}" \
    --reconnect-http "${RECONNECT_HTTP}" \
    --publisher-bin "${PUBLISHER_BIN}" \
    --subscriber-bin "${SUBSCRIBER_BIN}" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --publisher-endpoint "${PUBLISHER_ENDPOINT}" \
    --log-dir "${log_dir}" \
    --late-continue-file "${LATE_CONTINUE}" \
    >"${log_dir}/client-${suffix}.stdout.log" 2>"${log_dir}/client-${suffix}.stderr.log"
  cat "${log_dir}/client-${suffix}.stdout.log"
}

REGISTRY_PUB="tcp://127.0.0.1:$(pick_port)"
REGISTRY_ROUTER="tcp://127.0.0.1:$(pick_port)"
REGISTRY_HTTP="http://127.0.0.1:$(pick_port)"
PUBLISHER_ENDPOINT="tcp://127.0.0.1:$(pick_port)"
PUBLISHER_HTTP="http://127.0.0.1:$(pick_port)"
SUB1_HTTP="http://127.0.0.1:$(pick_port)"
SUB2_HTTP="http://127.0.0.1:$(pick_port)"
SUB3_HTTP="http://127.0.0.1:$(pick_port)"
RECONNECT_HTTP="http://127.0.0.1:$(pick_port)"

gradle_run installDist

PUBLISHER_READY="${log_dir}/publisher-ready"
PRELATE_CONTINUE="${log_dir}/prelate-continue"
LATE_READY="${log_dir}/late-ready"
LATE_CONTINUE="${log_dir}/late-continue"

start_registry
start_publisher publisher
PUBLISHER_PID="${LAST_PID}"

case "${SCENARIO}" in
  PS-A1|PS-A2|PS-C1)
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-A3)
    "${CLIENT_BIN}" \
      --mode "${SCENARIO}" \
      --publisher-http "${PUBLISHER_HTTP}" \
      --sub1-http "${SUB1_HTTP}" \
      --sub2-http "${SUB2_HTTP}" \
      --sub3-http "${SUB3_HTTP}" \
      --reconnect-http "${RECONNECT_HTTP}" \
      --publisher-bin "${PUBLISHER_BIN}" \
      --subscriber-bin "${SUBSCRIBER_BIN}" \
      --registry-router-endpoint "${REGISTRY_ROUTER}" \
      --publisher-endpoint "${PUBLISHER_ENDPOINT}" \
      --log-dir "${log_dir}" \
      --publisher-ready-file "${PUBLISHER_READY}" \
      --prelate-continue-file "${PRELATE_CONTINUE}" \
      --late-ready-file "${LATE_READY}" \
      --late-continue-file "${LATE_CONTINUE}" \
      >"${log_dir}/client-PS-A3.stdout.log" 2>"${log_dir}/client-PS-A3.stderr.log" &
    CLIENT_PID="$!"
    pids+=("${CLIENT_PID}")
    wait_marker "${PUBLISHER_READY}"
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    touch "${PRELATE_CONTINUE}"
    wait_marker "${LATE_READY}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    touch "${LATE_CONTINUE}"
    wait "${CLIENT_PID}"
    cat "${log_dir}/client-PS-A3.stdout.log"
    grep -q "scenario PS-A3 passed" "${log_dir}/client-PS-A3.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-A4)
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-B1)
    start_subscriber sub-1 alpha "${SUB1_HTTP}" 750
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    sleep "${ROUTE_SETTLE_SECONDS}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  PS-B2)
    start_subscriber sub-1 alpha "${SUB1_HTTP}"
    start_subscriber sub-2 beta "${SUB2_HTTP}"
    start_subscriber sub-3 gamma "${SUB3_HTTP}"
    run_client_mode "${SCENARIO}" "${SCENARIO}"
    grep -q "scenario ${SCENARIO} passed" "${log_dir}/client-${SCENARIO}.stdout.log"
    grep -Rq "message flow" "${log_dir}"/*-flow.log
    exit 0
    ;;
  all)
    ;;
  *)
    echo "Unknown PubSub scenario: ${SCENARIO}" >&2
    exit 1
    ;;
esac

"${CLIENT_BIN}" \
  --mode default \
  --publisher-http "${PUBLISHER_HTTP}" \
  --sub1-http "${SUB1_HTTP}" \
  --sub2-http "${SUB2_HTTP}" \
  --sub3-http "${SUB3_HTTP}" \
  --reconnect-http "${RECONNECT_HTTP}" \
  --publisher-bin "${PUBLISHER_BIN}" \
  --subscriber-bin "${SUBSCRIBER_BIN}" \
  --registry-router-endpoint "${REGISTRY_ROUTER}" \
  --publisher-endpoint "${PUBLISHER_ENDPOINT}" \
  --log-dir "${log_dir}" \
  --publisher-ready-file "${PUBLISHER_READY}" \
  --prelate-continue-file "${PRELATE_CONTINUE}" \
  --late-ready-file "${LATE_READY}" \
  --late-continue-file "${LATE_CONTINUE}" \
  >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log" &
CLIENT_PID="$!"
pids+=("${CLIENT_PID}")

wait_marker "${PUBLISHER_READY}"
start_subscriber sub-1 alpha "${SUB1_HTTP}"
SUB1_PID="${LAST_PID}"
start_subscriber sub-2 beta "${SUB2_HTTP}"
SUB2_PID="${LAST_PID}"
sleep "${ROUTE_SETTLE_SECONDS}"
touch "${PRELATE_CONTINUE}"

wait_marker "${LATE_READY}"
start_subscriber sub-3 gamma "${SUB3_HTTP}"
SUB3_PID="${LAST_PID}"
sleep "${ROUTE_SETTLE_SECONDS}"
touch "${LATE_CONTINUE}"

wait "${CLIENT_PID}"
cat "${log_dir}/client.stdout.log"

run_client_mode subscriber-restarted ps-a4

stop_pid "${SUB1_PID}"
start_subscriber sub-1 alpha "${SUB1_HTTP}" 750
SUB1_PID="${LAST_PID}"
sleep "${ROUTE_SETTLE_SECONDS}"
run_client_mode slow-subscriber ps-b1

run_client_mode publisher-restarted ps-b2

python3 - "${SUB1_HTTP}/evidence" >"${log_dir}/sub-1-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
python3 - "${SUB2_HTTP}/evidence" >"${log_dir}/sub-2-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
python3 - "${SUB3_HTTP}/evidence" >"${log_dir}/sub-3-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY

grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "HANDLER_MISSING/DROP/MissingEventMsg" "${log_dir}/sub-2-evidence.json"
