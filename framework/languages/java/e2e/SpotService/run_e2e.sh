#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/e2e-redis-common.sh"
SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
declare -A role_pids=()
REDIS_CONTAINER=""
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
SCENARIO="${1:-all}"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98"
echo "start_order=${E2E_START_ORDER}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/SpotService}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/SpotService-gradle-cache}"

if [[ "${SCENARIO}" != "all" && "${ZLINK_SPOT_SERVICE_RETRY_CHILD:-0}" != "1" && "${ZLINK_SPOT_SERVICE_ALL_CHILD:-0}" != "1" ]]; then
  output="$(mktemp)"
  scenario_passed=0
  for attempt in 1 2 3; do
    : >"${output}"
    set +e
    timeout 900s env ZLINK_SPOT_SERVICE_RETRY_CHILD=1 "${SCRIPT_PATH}" "${SCENARIO}" 2>&1 | tee "${output}"
    status="${PIPESTATUS[0]}"
    set -e
    if [[ "${status}" == "0" ]]; then
      scenario_passed=1
      break
    fi
    if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
      break
    fi
    if [[ "${attempt}" == "3" ]]; then
      break
    fi
    echo "spot-service transient bind failure; retrying scenario=${SCENARIO} (${attempt}/3)" >&2
    sleep 1
  done
  rm -f "${output}"
  if [[ "${scenario_passed}" == "1" ]]; then
    exit 0
  fi
  echo "scenario=${SCENARIO} failed" >&2
  exit 1
fi

if [[ "${SCENARIO}" == "all" && "${ZLINK_SPOT_SERVICE_ALL_CHILD:-0}" != "1" ]]; then
  for child_group in default-batch SM-F6 SM-G2 SM-G3 SM-G4 SM-G1 SM-Q9; do
    echo "child scenario=${child_group}"
    output="$(mktemp)"
    child_passed=0
    for attempt in 1 2 3; do
      : >"${output}"
      set +e
      timeout 900s env ZLINK_SPOT_SERVICE_ALL_CHILD=1 "${SCRIPT_PATH}" "${child_group}" 2>&1 | tee "${output}"
      status="${PIPESTATUS[0]}"
      set -e
      if [[ "${status}" == "0" ]]; then
        child_passed=1
        break
      fi
      if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
        break
      fi
      if [[ "${attempt}" == "3" ]]; then
        break
      fi
      echo "spot-service transient bind failure; retrying child scenario=${child_group} (${attempt}/3)" >&2
      sleep 1
    done
    rm -f "${output}"
    if [[ "${child_passed}" != "1" ]]; then
      echo "child scenario=${child_group} failed" >&2
      exit 1
    fi
  done
  echo "spot-service e2e result=passed"
  exit 0
fi

zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
  "zlink-redis-java-e2e" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="127.0.0.1:${redis_port}"
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:spot-service:${run_id}}"
LOCAL_READINESS_TIMEOUT_SECONDS="${LOCAL_READINESS_TIMEOUT_SECONDS:-30}"
LOCAL_READINESS_POLL_SECONDS="${LOCAL_READINESS_POLL_SECONDS:-0.1}"
LOCAL_READINESS_ATTEMPTS="${LOCAL_READINESS_ATTEMPTS:-300}"

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
  for _ in $(seq 1 50); do
    local alive=0
    for pid in "${pids[@]}"; do
      if kill -0 "${pid}" >/dev/null 2>&1; then
        alive=1
        break
      fi
    done
    [[ "${alive}" == "0" ]] && break
    sleep 0.1
  done
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
trap 'exit 130' INT
trap 'exit 143' TERM

reserve_ports() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(21):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:16]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[16:]))
finally:
    for sock in sockets:
        sock.close()
PY
}

reserve_client_endpoints() {
  python3 - <<'PY'
import socket
sockets = []
endpoints = []
try:
    for _ in range(2):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        endpoints.append(f"tcp://127.0.0.1:{sock.getsockname()[1]}")
    print(" ".join(endpoints))
finally:
    for sock in sockets:
        sock.close()
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

ordered_roles() {
  python3 - "${E2E_START_ORDER}" "$@" <<'PY'
import random
import sys

mode = sys.argv[1]
roles = sys.argv[2:]
if mode in ("", "forward"):
    pass
elif mode == "reverse":
    roles.reverse()
elif mode.startswith("shuffle:"):
    seed_text = mode.split(":", 1)[1]
    if seed_text == "":
        raise SystemExit("E2E_START_ORDER shuffle requires a seed")
    random.Random(int(seed_text)).shuffle(roles)
else:
    raise SystemExit(f"unsupported E2E_START_ORDER={mode!r}")
for role in roles:
    print(role)
PY
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon --no-parallel --max-workers=1 "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/spot-service-client/bin/spot-service-client"
}

play_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Play/install/spot-service-play/bin/spot-service-play"
}

publisher_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Publisher/install/spot-service-publisher/bin/spot-service-publisher"
}

gateway_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Gateway/install/spot-service-gateway/bin/spot-service-gateway"
}

multi_node_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-MultiNode/install/spot-service-multi-node/bin/spot-service-multi-node"
}

start_play() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local ingress="$4"
  local http="$5"
  local spot_pub="$6"
  local stream="$7"
  local tls_stream="$8"
  ZLINK_JAVA_E2E_NODE_RID="${rid}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_JAVA_E2E_INGRESS_ENDPOINT="${ingress}" \
  ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT="${INGRESS_A}" \
  ZLINK_JAVA_E2E_INGRESS_B_ENDPOINT="${INGRESS_B}" \
  ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_JAVA_E2E_SPOT_ENDPOINT="${spot}" \
  ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT="${spot_pub}" \
  ZLINK_JAVA_E2E_STREAM_ENDPOINT="${stream}" \
  ZLINK_JAVA_E2E_SPOT_A_ENDPOINT="${SPOT_A}" \
  ZLINK_JAVA_E2E_SPOT_B_ENDPOINT="${SPOT_B}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_JAVA_E2E_TLS_STREAM_ENDPOINT="${tls_stream}" \
  ZLINK_JAVA_E2E_TLS_CERTIFICATE_PATH="${TLS_CERTIFICATE_PATH:-}" \
  ZLINK_JAVA_E2E_TLS_KEY_PATH="${TLS_KEY_PATH:-}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(play_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  pids+=("$!")
}

start_gateway() {
  ZLINK_JAVA_E2E_GATEWAY_RID="client-route-mesh" \
  ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT="${HTTP_GATEWAY}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_CLIENT}" \
  ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_CLIENT}" \
  ZLINK_JAVA_E2E_SPOT_A_ENDPOINT="${SPOT_A}" \
  ZLINK_JAVA_E2E_SPOT_B_ENDPOINT="${SPOT_B}" \
  ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT="${INGRESS_A}" \
  ZLINK_JAVA_E2E_STREAM_A_ENDPOINT="${STREAM_A}" \
  ZLINK_JAVA_E2E_STREAM_B_ENDPOINT="${STREAM_B}" \
  ZLINK_JAVA_E2E_TLS_STREAM_A_ENDPOINT="${TLS_STREAM_A}" \
  ZLINK_JAVA_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
  ZLINK_JAVA_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
  ZLINK_JAVA_E2E_MULTI_A_HTTP_ENDPOINT="${MULTI_HTTP_A}" \
  ZLINK_JAVA_E2E_MULTI_B_HTTP_ENDPOINT="${MULTI_HTTP_B}" \
  ZLINK_JAVA_E2E_SM_G1_READY_FILE="${log_dir}/sm-g1-ready" \
  ZLINK_JAVA_E2E_SM_G1_CRASHED_FILE="${log_dir}/sm-g1-crashed" \
  ZLINK_JAVA_E2E_SM_G1_FAILED_FILE="${log_dir}/sm-g1-failed" \
  ZLINK_JAVA_E2E_SM_G1_RESTARTED_FILE="${log_dir}/sm-g1-restarted" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(gateway_bin)" >"${log_dir}/gateway.stdout.log" 2>"${log_dir}/gateway.stderr.log" &
  pids+=("$!")
}

start_multi_node() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local http="$4"
  ZLINK_JAVA_E2E_NODE_RID="${rid}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_JAVA_E2E_SPOT_ENDPOINT="${spot}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_JAVA_E2E_SPOT_ONLY="${SPOT_ONLY_MODE}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(multi_node_bin)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  pids+=("$!")
}

start_named_server() {
  case "$1" in
    play-a) start_play play-a "${ROUTE_A}" "${SPOT_A}" "${INGRESS_A}" "${HTTP_A}" "${SPOT_PUB_A}" "${STREAM_A}" "${TLS_STREAM_A}" ;;
    play-b) start_play play-b "${ROUTE_B}" "${SPOT_B}" "${INGRESS_B}" "${HTTP_B}" "${SPOT_PUB_B}" "${STREAM_B}" "" ;;
    gateway) start_gateway ;;
    multi-node-a) start_multi_node multi-node-a "${ROUTE_A}" "${MULTI_SPOT_A}" "${MULTI_HTTP_A}" ;;
    multi-node-b) start_multi_node multi-node-b "${ROUTE_B}" "${MULTI_SPOT_B}" "${MULTI_HTTP_B}" ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
  role_pids["$1"]="${pids[$((${#pids[@]} - 1))]}"
}

crash_named_server() {
  local role="$1"
  local pid="${role_pids[$role]:-}"
  if [[ -z "${pid}" ]]; then
    echo "No pid recorded for role '${role}'" >&2
    return 1
  fi
  for child in $(descendants "${pid}"); do
    kill -9 "${child}" >/dev/null 2>&1 || true
  done
  kill -9 "${pid}" >/dev/null 2>&1 || true
  wait "${pid}" >/dev/null 2>&1 || true
  unset "role_pids[$role]"
}

wait_named_server() {
  case "$1" in
    play-a)
      wait_port play-a-route "${ROUTE_A}"
      wait_port play-a-spot "${SPOT_A}"
      wait_port play-a-spot-pub "${SPOT_PUB_A}"
      wait_port play-a-stream "${STREAM_A}"
      if [[ -n "${TLS_STREAM_A}" ]]; then
        wait_port play-a-tls-stream "${TLS_STREAM_A}"
      fi
      wait_port play-a-http "${HTTP_A}"
      ;;
    play-b)
      wait_port play-b-route "${ROUTE_B}"
      wait_port play-b-spot "${SPOT_B}"
      wait_port play-b-spot-pub "${SPOT_PUB_B}"
      wait_port play-b-stream "${STREAM_B}"
      wait_port play-b-http "${HTTP_B}"
      ;;
    gateway)
      wait_port gateway-route "${ROUTE_CLIENT}"
      wait_port gateway-spot "${SPOT_CLIENT}"
      wait_port gateway-http "${HTTP_GATEWAY}"
      ;;
    multi-node-a)
      if [[ "${SPOT_ONLY_MODE}" != "true" ]]; then
        wait_port multi-node-a-route "${ROUTE_A}"
      fi
      wait_port multi-node-a-spot "${MULTI_SPOT_A}"
      wait_port multi-node-a-http "${MULTI_HTTP_A}"
      ;;
    multi-node-b)
      if [[ "${SPOT_ONLY_MODE}" != "true" ]]; then
        wait_port multi-node-b-route "${ROUTE_B}"
      fi
      wait_port multi-node-b-spot "${MULTI_SPOT_B}"
      wait_port multi-node-b-http "${MULTI_HTTP_B}"
      ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

run_publisher() {
  ZLINK_JAVA_E2E_SPOT_PUBLISHER_ENDPOINT="${SPOT_PUBLISHER}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    timeout -k 5s 30s "$(publisher_bin)" >"${log_dir}/publisher.stdout.log" 2>"${log_dir}/publisher.stderr.log"
}

fetch_evidence() {
  local name="$1"
  local endpoint="$2"
  python3 - "${endpoint}/evidence" >"${log_dir}/${name}-evidence.json" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
}

fetch_default_evidence() {
  fetch_evidence play-a "${HTTP_A}"
  fetch_evidence play-b "${HTTP_B}"
}

fetch_started_standard_evidence() {
  if [[ -n "${role_pids[play-a]:-}" ]]; then
    fetch_evidence play-a "${HTTP_A}"
  fi
  if [[ -n "${role_pids[play-b]:-}" ]]; then
    fetch_evidence play-b "${HTTP_B}"
  fi
}

assert_evidence_contains() {
  local pattern="$1"
  shift
  grep -q "${pattern}" "$@"
}

generate_tls_cert() {
  local cert="$1"
  local key="$2"
  if ! command -v openssl >/dev/null 2>&1; then
    echo "openssl is required for SM-D14 TLS certificate generation" >&2
    return 1
  fi
  openssl req -x509 -newkey rsa:2048 -nodes -days 7 \
    -subj /CN=localhost \
    -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
    -keyout "${key}" \
    -out "${cert}" >/dev/null 2>&1
}

close_spot() {
  local endpoint="$1"
  local rid="$2"
  python3 - "${endpoint}/admin/close?rid=${rid}" <<'PY'
import sys
import urllib.request
request = urllib.request.Request(sys.argv[1], method="POST")
with urllib.request.urlopen(request, timeout=5) as response:
    body = response.read().decode("utf-8")
    if '"closed":true' not in body:
        raise SystemExit("spot close did not report closed=true: " + body)
PY
}

create_timer_spot() {
  local endpoint="$1"
  local rid="$2"
  python3 - "${endpoint}/admin/create-timer?rid=${rid}" <<'PY'
import sys
import urllib.request
request = urllib.request.Request(sys.argv[1], method="POST")
with urllib.request.urlopen(request, timeout=5) as response:
    body = response.read().decode("utf-8")
    if '"created":true' not in body:
        raise SystemExit("timer spot create did not report created=true: " + body)
PY
}

assert_type_mismatch() {
  local endpoint="$1"
  local rid="$2"
  python3 - "${endpoint}/admin/type-mismatch?rid=${rid}" <<'PY'
import sys
import urllib.request
request = urllib.request.Request(sys.argv[1], method="POST")
with urllib.request.urlopen(request, timeout=5) as response:
    body = response.read().decode("utf-8")
    if '"mismatch":true' not in body:
        raise SystemExit("spot type mismatch did not report mismatch=true: " + body)
PY
}

read -r ROUTE_A ROUTE_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_CLIENT INGRESS_A INGRESS_B SPOT_PUB_A SPOT_PUB_B SPOT_PUBLISHER STREAM_A STREAM_B TLS_STREAM_A_RAW MULTI_SPOT_A MULTI_SPOT_B HTTP_A HTTP_B HTTP_GATEWAY MULTI_HTTP_A MULTI_HTTP_B <<<"$(reserve_ports)"
TLS_STREAM_A=""
if [[ "${SCENARIO}" == "SM-D14" || "${SCENARIO}" == "sm-d14" || "${ZLINK_JAVA_E2E_MODES:-}" == *stream-tls* ]]; then
  TLS_STREAM_A="${TLS_STREAM_A_RAW/tcp:/tls:}"
  TLS_CERTIFICATE_PATH="${log_dir}/sm-d14-cert.pem"
  TLS_KEY_PATH="${log_dir}/sm-d14-key.pem"
  generate_tls_cert "${TLS_CERTIFICATE_PATH}" "${TLS_KEY_PATH}"
fi
SPOT_ONLY_MODE="false"
if [[ "${SCENARIO}" == "SM-F6" || "${SCENARIO}" == "sm-f6" || "${ZLINK_JAVA_E2E_MODES:-}" == *spot-only-mesh* ]]; then
  SPOT_ONLY_MODE="true"
fi

gradle_run installDist

if [[ "${SCENARIO}" == "SM-E1" || "${SCENARIO}" == "sm-e1" ]]; then
  SERVER_ROLES=(play-a gateway)
elif [[ "${SCENARIO}" == "SM-Q9" || "${SCENARIO}" == "sm-q9" || "${SPOT_ONLY_MODE}" == "true" || "${ZLINK_JAVA_E2E_MODES:-}" == *multi-node* ]]; then
  SERVER_ROLES=(multi-node-a multi-node-b gateway)
else
  SERVER_ROLES=(play-a play-b gateway)
fi
mapfile -t ORDERED_SERVER_ROLES < <(ordered_roles "${SERVER_ROLES[@]}")
for role in "${ORDERED_SERVER_ROLES[@]}"; do
  start_named_server "$role"
  wait_named_server "$role"
done
sleep 2

run_client_mode() {
  local mode="$1"
  local timeout_seconds="${ZLINK_JAVA_E2E_CLIENT_TIMEOUT_SECONDS:-90}"
  local attempt
  local status
  for attempt in $(seq 1 5); do
    set +e
      ZLINK_JAVA_E2E_CLIENT_MODE="${mode}" \
      ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT="${HTTP_GATEWAY}" \
      ZLINK_JAVA_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
      ZLINK_JAVA_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
      ZLINK_JAVA_E2E_TLS_STREAM_A_ENDPOINT="${TLS_STREAM_A}" \
      ZLINK_JAVA_E2E_MULTI_A_HTTP_ENDPOINT="${MULTI_HTTP_A}" \
      ZLINK_JAVA_E2E_MULTI_B_HTTP_ENDPOINT="${MULTI_HTTP_B}" \
      ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
        timeout -k 5s "${timeout_seconds}s" "$(client_bin)" >"${log_dir}/client-${mode}.stdout.log" 2>"${log_dir}/client-${mode}.stderr.log"
    status="$?"
    set -e
    if [[ "${status}" == "0" ]] && grep -q "spot-service e2e mode=${mode} result=passed" "${log_dir}/client-${mode}.stdout.log"; then
      cat "${log_dir}/client-${mode}.stdout.log" >>"${log_dir}/client.stdout.log"
      cat "${log_dir}/client-${mode}.stderr.log" >>"${log_dir}/client.stderr.log"
      return 0
    fi
    sleep 1
  done
  return 1
}

wait_file() {
  local name="$1"
  local path="$2"
  local deadline=$((SECONDS + 60))
  while (( SECONDS < deadline )); do
    if [[ -f "${path}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${path}" >&2
  return 1
}

run_sm_g1() {
  local ready_file="${log_dir}/sm-g1-ready"
  local crashed_file="${log_dir}/sm-g1-crashed"
  local failed_file="${log_dir}/sm-g1-failed"
  local restarted_file="${log_dir}/sm-g1-restarted"
  local client_pid

  ZLINK_JAVA_E2E_CLIENT_MODE="play-crash-recovery" \
  ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT="${HTTP_GATEWAY}" \
  ZLINK_JAVA_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
  ZLINK_JAVA_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
  ZLINK_JAVA_E2E_SM_G1_READY_FILE="${ready_file}" \
  ZLINK_JAVA_E2E_SM_G1_CRASHED_FILE="${crashed_file}" \
  ZLINK_JAVA_E2E_SM_G1_FAILED_FILE="${failed_file}" \
  ZLINK_JAVA_E2E_SM_G1_RESTARTED_FILE="${restarted_file}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    timeout -k 5s 180s "$(client_bin)" >"${log_dir}/client-play-crash-recovery.stdout.log" 2>"${log_dir}/client-play-crash-recovery.stderr.log" &
  client_pid="$!"
  pids+=("${client_pid}")

  wait_file "SM-G1 ready signal" "${ready_file}"
  crash_named_server play-a
  touch "${crashed_file}"
  wait_file "SM-G1 bounded failure signal" "${failed_file}"
  sleep "${ZLINK_JAVA_E2E_SM_G1_LEASE_WAIT_SECONDS:-20}"
  start_named_server play-a
  wait_named_server play-a
  sleep 2
  touch "${restarted_file}"

  wait "${client_pid}"
  grep -q "spot-service e2e mode=play-crash-recovery result=passed" "${log_dir}/client-play-crash-recovery.stdout.log"
  cat "${log_dir}/client-play-crash-recovery.stdout.log" >>"${log_dir}/client.stdout.log"
  cat "${log_dir}/client-play-crash-recovery.stderr.log" >>"${log_dir}/client.stderr.log"
}

scenario_modes() {
  case "$1" in
    all|default-batch)
      echo "state1 state2 send normal worker missing timeout owner spot-outbound spot-to-spot spot-mesh-cross-node route-mesh actor-session actor-missing actor-destroy actor-join-admission actor-push-chain actor-leave-disconnect actor-disconnect-notify idle-timer timer-overrun"
      ;;
    SM-A1) echo "state1" ;;
    SM-A2) echo "state2" ;;
    SM-A3|SM-A4) echo "owner" ;;
    SM-A5) echo "stage-wrapper" ;;
    SM-A6) echo "owner" ;;
    SM-A7) echo "owner" ;;
    SM-A8) echo "worker" ;;
    SM-B1|SM-B3|SM-B7|SM-D1|SM-D3|SM-D9) echo "actor-session" ;;
    SM-B2|SM-B4|SM-D2) echo "remote-actor-session" ;;
    SM-B5) echo "actor-missing" ;;
    SM-B6) echo "actor-leave-disconnect" ;;
    SM-B8) echo "actor-destroy" ;;
    SM-B9) echo "actor-join-admission" ;;
    SM-C1) echo "normal" ;;
    SM-C2) echo "spot-outbound" ;;
    SM-C3) echo "spot-to-spot" ;;
    SM-C4) echo "spot-mesh-cross-node" ;;
    SM-C5) echo "spot-mesh-cross-node" ;;
    SM-D5) echo "actor-disconnect-notify" ;;
    SM-D6) echo "bound-push-isolation" ;;
    SM-D7) echo "stream-auth" ;;
    SM-D8) echo "stream-reconnect" ;;
    SM-D4) echo "multi-actor-bind" ;;
    SM-D10) echo "stream-backpressure" ;;
    SM-D11) echo "mixed-stream-channel" ;;
    SM-D12) echo "stream-rebind-transfer" ;;
    SM-D13) echo "stream-heartbeat" ;;
    SM-D14) echo "stream-tls" ;;
    SM-D15) echo "actor-push-chain" ;;
    SM-Q9) echo "multi-node" ;;
    SM-E1) echo "missing" ;;
    SM-E2) echo "state1" ;;
    SM-E3) echo "idle-timer" ;;
    SM-E4) echo "timer-overrun" ;;
    SM-G2) echo "owner-remap" ;;
    SM-G3) echo "join-leave-race" ;;
    SM-G4) echo "bound-push-load" ;;
    SM-F1|SM-F2|SM-F4) echo "route-mesh" ;;
    SM-F5) echo "route-lifecycle" ;;
    SM-F6) echo "spot-only-mesh" ;;
    *)
      echo "SpotService Java scenario $1 is not mapped to an implemented client mode" >&2
      return 1
      ;;
  esac
}

: >"${log_dir}/client.stdout.log"
: >"${log_dir}/client.stderr.log"
if [[ "${SCENARIO}" == "SM-G1" || "${SCENARIO}" == "sm-g1" ]]; then
  run_sm_g1
  cat "${log_dir}/client.stdout.log"
  fetch_evidence play-a "${HTTP_A}"
  fetch_evidence play-b "${HTTP_B}"
  exit 0
fi
client_modes="${ZLINK_JAVA_E2E_MODES:-$(scenario_modes "${SCENARIO}")}"
for mode in ${client_modes}; do
  if [[ "${mode}" == "idle-timer" ]]; then
    create_timer_spot "${HTTP_A}" idle-close
    create_timer_spot "${HTTP_A}" idle-active
    sleep 2
  fi
  if [[ "${mode}" == "timer-overrun" ]]; then
    create_timer_spot "${HTTP_A}" timer-overrun-skip
    create_timer_spot "${HTTP_A}" timer-overrun-catchup
    create_timer_spot "${HTTP_A}" timer-overrun-delay
    sleep 2
  fi
  run_client_mode "${mode}"
  if [[ "${mode}" == "idle-timer" ]]; then
    close_spot "${HTTP_A}" idle-active
  fi
  sleep 2
done
if [[ -n "${ZLINK_JAVA_E2E_MODES:-}" || ( "${SCENARIO}" != "all" && "${SCENARIO}" != "default-batch" ) ]]; then
  case "${SCENARIO}" in
    SM-C4|sm-c4)
      run_publisher
      cat "${log_dir}/publisher.stdout.log" >>"${log_dir}/client.stdout.log"
      cat "${log_dir}/publisher.stderr.log" >>"${log_dir}/client.stderr.log"
      ;;
    SM-A7|sm-a7)
      assert_type_mismatch "${HTTP_A}" room-a
      echo "scenario SM-A7 passed" >>"${log_dir}/client.stdout.log"
      ;;
    SM-E2|sm-e2)
      sleep 2
      echo "scenario SM-E2 passed" >>"${log_dir}/client.stdout.log"
      ;;
    SM-A6|sm-a6)
      close_spot "${HTTP_B}" room-b
      echo "scenario SM-A6 passed" >>"${log_dir}/client.stdout.log"
      ;;
  esac
  cat "${log_dir}/client.stdout.log"
  if [[ "${SCENARIO}" == "SM-Q9" || "${SCENARIO}" == "sm-q9" || "${SPOT_ONLY_MODE}" == "true" || "${ZLINK_JAVA_E2E_MODES:-}" == *multi-node* ]]; then
    fetch_evidence multi-node-a "${MULTI_HTTP_A}"
    fetch_evidence multi-node-b "${MULTI_HTTP_B}"
  else
    fetch_started_standard_evidence
  fi
  case "${SCENARIO}" in
    SM-C4|sm-c4)
      assert_evidence_contains "SpotMeshMsg" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
      ;;
    SM-A7|sm-a7)
      assert_evidence_contains "SpotTypeMismatch" "${log_dir}/play-a-evidence.json"
      assert_evidence_contains "SpotTypeMismatchStateOk" "${log_dir}/play-a-evidence.json"
      ;;
    SM-E2|sm-e2)
      assert_evidence_contains "SpotTimer" "${log_dir}/play-a-evidence.json"
      ;;
    SM-A6|sm-a6)
      assert_evidence_contains "SpotClosing" "${log_dir}/play-b-evidence.json"
      ;;
  esac
  exit 0
fi
run_publisher
cat "${log_dir}/publisher.stdout.log" >>"${log_dir}/client.stdout.log"
cat "${log_dir}/publisher.stderr.log" >>"${log_dir}/client.stderr.log"
sleep 2
assert_type_mismatch "${HTTP_A}" room-a
echo "scenario SM-A7 passed" >>"${log_dir}/client.stdout.log"
echo "scenario SM-E2 passed" >>"${log_dir}/client.stdout.log"
close_spot "${HTTP_B}" room-b
echo "scenario SM-A6 passed" >>"${log_dir}/client.stdout.log"

cat "${log_dir}/client.stdout.log"
fetch_default_evidence
grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "packet=RouteReq" "${log_dir}/gateway-flow.log"
grep -q '"marker":"RouteReq"' "${log_dir}/play-a-evidence.json"
grep -q '"value":"route-mesh-normal"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorCreated"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorCreatedPayload"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorDestroyed"' "${log_dir}/play-a-evidence.json"
grep -q '"value":"Player One/7/alpha,beta"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorUserJoined"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorUserLeft"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorUserDisconnected"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorUserReq"' "${log_dir}/play-a-evidence.json"
grep -q 'ActorCreated.*ActorUserJoined.*ActorUserReq' "${log_dir}/play-a-evidence.json"
grep -q 'user-echo-1.*user-echo-2.*user-echo-3' "${log_dir}/play-a-evidence.json"
if grep -q '"marker":"ActorCreated"' "${log_dir}/play-b-evidence.json"; then
  echo "unexpected play-b actor creation evidence" >&2
  exit 1
fi
grep -q '"marker":"StreamInbound"' "${log_dir}/play-a-evidence.json"
grep -q "DispatchError" "${log_dir}/play-a-evidence.json"
grep -q "MissingActorReq" "${log_dir}/play-a-evidence.json"
grep -q "SpotInitialized" "${log_dir}/play-a-evidence.json"
grep -q "SpotClosing" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
grep -q "SpotTypeMismatch" "${log_dir}/play-a-evidence.json"
grep -q "SpotTypeMismatchStateOk" "${log_dir}/play-a-evidence.json"
grep -q "SpotTimer" "${log_dir}/play-a-evidence.json"
grep -q "WorkerStarted" "${log_dir}/play-a-evidence.json"
grep -q "WorkerFollowUpBeforeComplete" "${log_dir}/play-a-evidence.json"
grep -q "WorkerCompleted" "${log_dir}/play-a-evidence.json"
grep -q "IngressReq" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
grep -q "IngressMsg" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
grep -q "SpotOutbound" "${log_dir}/play-a-evidence.json"
grep -q "SpotToSpotSend" "${log_dir}/play-b-evidence.json"
grep -q "SpotMeshMsg" "${log_dir}/play-a-evidence.json"
grep -q "SpotMeshMsg" "${log_dir}/play-b-evidence.json"
grep -q "IdleCloseRequested" "${log_dir}/play-a-evidence.json"
grep -q "IdleClosed" "${log_dir}/play-a-evidence.json"
grep -q "IdleKeptOpen" "${log_dir}/play-a-evidence.json"
grep -q "TimerOverrunConfigured" "${log_dir}/play-a-evidence.json"
grep -q "TimerOverrunTick" "${log_dir}/play-a-evidence.json"
echo "spot-service e2e result=passed"
