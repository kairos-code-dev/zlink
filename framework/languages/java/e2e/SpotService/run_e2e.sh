#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.spotservice\.(client|gateway|play|publisher)\.Program'
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
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/SpotService}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/SpotService-gradle-cache}"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT:-${ZLINK_REDIS_LOCATION_ENDPOINT:-127.0.0.1:16379}}"
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:spot-service:${run_id}}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30

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
  (pgrep -f "${role_pattern}" 2>/dev/null || true) | while read -r pid; do
    kill -9 "${pid}" >/dev/null 2>&1 || true
  done
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
    for _ in range(16):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:13]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[13:]))
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

start_play() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local ingress="$4"
  local http="$5"
  local spot_pub="$6"
  local stream="$7"
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
  ZLINK_JAVA_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
  ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(gateway_bin)" >"${log_dir}/gateway.stdout.log" 2>"${log_dir}/gateway.stderr.log" &
  pids+=("$!")
}

start_named_server() {
  case "$1" in
    play-a) start_play play-a "${ROUTE_A}" "${SPOT_A}" "${INGRESS_A}" "${HTTP_A}" "${SPOT_PUB_A}" "${STREAM_A}" ;;
    play-b) start_play play-b "${ROUTE_B}" "${SPOT_B}" "${INGRESS_B}" "${HTTP_B}" "${SPOT_PUB_B}" "${STREAM_B}" ;;
    gateway) start_gateway ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

wait_named_server() {
  case "$1" in
    play-a)
      wait_port play-a-route "${ROUTE_A}"
      wait_port play-a-spot "${SPOT_A}"
      wait_port play-a-spot-pub "${SPOT_PUB_A}"
      wait_port play-a-stream "${STREAM_A}"
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

read -r ROUTE_A ROUTE_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_CLIENT INGRESS_A INGRESS_B SPOT_PUB_A SPOT_PUB_B SPOT_PUBLISHER STREAM_A STREAM_B HTTP_A HTTP_B HTTP_GATEWAY <<<"$(reserve_ports)"

gradle_run installDist

SERVER_ROLES=(play-a play-b gateway)
mapfile -t ORDERED_SERVER_ROLES < <(ordered_roles "${SERVER_ROLES[@]}")
for role in "${ORDERED_SERVER_ROLES[@]}"; do
  start_named_server "$role"
  wait_named_server "$role"
done
sleep 2

run_client_mode() {
  local mode="$1"
  local attempt
  local status
  for attempt in $(seq 1 5); do
    set +e
    ZLINK_JAVA_E2E_CLIENT_MODE="${mode}" \
      ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT="${HTTP_GATEWAY}" \
      ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
        timeout -k 5s 30s "$(client_bin)" >"${log_dir}/client-${mode}.stdout.log" 2>"${log_dir}/client-${mode}.stderr.log"
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

scenario_modes() {
  case "$1" in
    all)
      echo "state1 state2 send normal worker missing timeout owner spot-outbound spot-to-spot route-mesh actor-session actor-leave-disconnect actor-disconnect-notify idle-timer timer-overrun"
      ;;
    SM-A1) echo "state1" ;;
    SM-A2) echo "state2" ;;
    SM-A3|SM-A4) echo "owner" ;;
    SM-A8) echo "worker" ;;
    SM-B1|SM-B3|SM-B7|SM-D1) echo "actor-session" ;;
    SM-B6) echo "actor-leave-disconnect" ;;
    SM-C1) echo "normal" ;;
    SM-C2) echo "spot-outbound" ;;
    SM-C3) echo "spot-to-spot" ;;
    SM-D5) echo "actor-disconnect-notify" ;;
    SM-E1) echo "missing" ;;
    SM-E3) echo "idle-timer" ;;
    SM-E4) echo "timer-overrun" ;;
    SM-F1|SM-F2|SM-F4) echo "route-mesh" ;;
    *)
      echo "SpotService Java scenario $1 is not mapped to an implemented client mode" >&2
      return 1
      ;;
  esac
}

: >"${log_dir}/client.stdout.log"
: >"${log_dir}/client.stderr.log"
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
if [[ -n "${ZLINK_JAVA_E2E_MODES:-}" || "${SCENARIO}" != "all" ]]; then
  cat "${log_dir}/client.stdout.log"
  fetch_evidence play-a "${HTTP_A}"
  fetch_evidence play-b "${HTTP_B}"
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
fetch_evidence play-a "${HTTP_A}"
fetch_evidence play-b "${HTTP_B}"
grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "packet=RouteReq" "${log_dir}/gateway-flow.log"
grep -q '"marker":"RouteReq"' "${log_dir}/play-a-evidence.json"
grep -q '"value":"route-mesh-normal"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorCreated"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorCreatedPayload"' "${log_dir}/play-a-evidence.json"
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
if grep -q '"marker":"ActorUserJoined"' "${log_dir}/play-b-evidence.json"; then
  echo "unexpected play-b actor join evidence" >&2
  exit 1
fi
grep -q '"marker":"StreamInbound"' "${log_dir}/play-a-evidence.json"
grep -q "DispatchError" "${log_dir}/play-a-evidence.json"
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
