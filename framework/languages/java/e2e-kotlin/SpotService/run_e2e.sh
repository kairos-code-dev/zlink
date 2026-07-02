#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
SESSION_A_PID=""
SESSION_B_PID=""
GATEWAY_PID=""
MULTI_NODE_A_PID=""
MULTI_NODE_B_PID=""
role_pattern='systems\.zlink\.e2e\.kotlin\.spotservice\..*(RegistryProgram|PlayProgram|GatewayProgram|MultiNodeProgram|SessionProgram|ClientProgram)Kt'
client_role_pattern='systems\.zlink\.e2e\.kotlin\.spotservice\..*ClientProgramKt'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
SCENARIO="${1:-all}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_KOTLIN_E2E_BUILD_DIR="${ZLINK_KOTLIN_E2E_BUILD_DIR:-${HOME}/.cache/zlink/kotlin-e2e/SpotService}"
export ZLINK_KOTLIN_E2E_GRADLE_CACHE="${ZLINK_KOTLIN_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/kotlin-e2e/SpotService-gradle-cache}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
PROCESS_STOP_TIMEOUT_SECONDS=5
PROCESS_STOP_ATTEMPTS=50
ROUTE_SETTLE_SECONDS=5
MODE_RETRY_SETTLE_SECONDS=1

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
    for _ in range(17):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:15]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[15:]))
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

reserve_session_endpoints() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(3):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(f"tcp://127.0.0.1:{ports[0]} tcp://127.0.0.1:{ports[1]} http://127.0.0.1:{ports[2]}")
finally:
    for sock in sockets:
        sock.close()
PY
}

reserve_gateway_endpoints() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(2):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(f"tcp://127.0.0.1:{ports[0]} http://127.0.0.1:{ports[1]}")
finally:
    for sock in sockets:
        sock.close()
PY
}

reserve_multinode_endpoints() {
  python3 - <<'PY'
import socket
sockets = []
ports = []
try:
    for _ in range(6):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(f"tcp://127.0.0.1:{port}" for port in ports[:4]), end=" ")
    print(" ".join(f"http://127.0.0.1:{port}" for port in ports[4:]))
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
  echo "Timed out after ${LOCAL_READINESS_TIMEOUT_SECONDS}s waiting for ${name} at ${endpoint}" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_KOTLIN_E2E_GRADLE_CACHE}" --no-daemon --no-parallel --max-workers=1 "$@" --quiet
}

role_bin() {
  case "$1" in
    client)
      echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Client/install/spot-service-kotlin-client/bin/spot-service-kotlin-client"
      ;;
    play)
      echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Play/install/spot-service-kotlin-play/bin/spot-service-kotlin-play"
      ;;
    gateway)
      echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Gateway/install/spot-service-kotlin-gateway/bin/spot-service-kotlin-gateway"
      ;;
    multi-node)
      echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-MultiNode/install/spot-service-kotlin-multi-node/bin/spot-service-kotlin-multi-node"
      ;;
    session)
      echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Session/install/spot-service-kotlin-session/bin/spot-service-kotlin-session"
      ;;
    registry)
      echo "${ZLINK_KOTLIN_E2E_BUILD_DIR}/Server-Registry/install/spot-service-kotlin-registry/bin/spot-service-kotlin-registry"
      ;;
    *)
      echo "unknown role $1" >&2
      return 1
      ;;
  esac
}

start_registry() {
  ZLINK_KOTLIN_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(role_bin registry)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
  pids+=("$!")
  wait_port registry-router "${REGISTRY_ROUTER}"
}

start_play() {
  local rid="$1"
  local route="$2"
  local spot="$3"
  local ingress="$4"
  local http="$5"
  local spot_pub="$6"
  local stream="$7"
  ZLINK_KOTLIN_E2E_NODE_RID="${rid}" \
  ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT="${route}" \
  ZLINK_KOTLIN_E2E_INGRESS_ENDPOINT="${ingress}" \
  ZLINK_KOTLIN_E2E_INGRESS_A_ENDPOINT="${INGRESS_A}" \
  ZLINK_KOTLIN_E2E_INGRESS_B_ENDPOINT="${INGRESS_B}" \
  ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
  ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
  ZLINK_KOTLIN_E2E_SPOT_ENDPOINT="${spot}" \
  ZLINK_KOTLIN_E2E_SPOT_PUB_ENDPOINT="${spot_pub}" \
  ZLINK_KOTLIN_E2E_STREAM_ENDPOINT="${stream}" \
  ZLINK_KOTLIN_E2E_SPOT_A_ENDPOINT="${SPOT_A}" \
  ZLINK_KOTLIN_E2E_SPOT_B_ENDPOINT="${SPOT_B}" \
  ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${http}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(role_bin play)" >"${log_dir}/${rid}.stdout.log" 2>"${log_dir}/${rid}.stderr.log" &
  pids+=("$!")
  wait_port "${rid}-route" "${route}"
  wait_port "${rid}-spot" "${spot}"
  wait_port "${rid}-spot-pub" "${spot_pub}"
  if [[ -n "${stream}" ]]; then
    wait_port "${rid}-stream" "${stream}"
  fi
  wait_port "${rid}-http" "${http}"
}

start_session() {
  read -r SPOT_SESSION_A STREAM_SESSION_A HTTP_SESSION_A <<<"$(reserve_session_endpoints)"
  read -r SPOT_SESSION_B STREAM_SESSION_B HTTP_SESSION_B <<<"$(reserve_session_endpoints)"
  ZLINK_KOTLIN_E2E_NODE_RID="session-a" \
  ZLINK_KOTLIN_E2E_SPOT_ENDPOINT="${SPOT_SESSION_A}" \
  ZLINK_KOTLIN_E2E_STREAM_ENDPOINT="${STREAM_SESSION_A}" \
  ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${HTTP_SESSION_A}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(role_bin session)" >"${log_dir}/session-a.stdout.log" 2>"${log_dir}/session-a.stderr.log" &
  SESSION_A_PID="$!"
  pids+=("$!")
  ZLINK_KOTLIN_E2E_NODE_RID="session-b" \
  ZLINK_KOTLIN_E2E_SPOT_ENDPOINT="${SPOT_SESSION_B}" \
  ZLINK_KOTLIN_E2E_STREAM_ENDPOINT="${STREAM_SESSION_B}" \
  ZLINK_KOTLIN_E2E_HTTP_ENDPOINT="${HTTP_SESSION_B}" \
  ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
    "$(role_bin session)" >"${log_dir}/session-b.stdout.log" 2>"${log_dir}/session-b.stderr.log" &
  SESSION_B_PID="$!"
  pids+=("$!")
  wait_port session-a-spot "${SPOT_SESSION_A}"
  wait_port session-a-stream "${STREAM_SESSION_A}"
  wait_port session-a-http "${HTTP_SESSION_A}"
  wait_port session-b-spot "${SPOT_SESSION_B}"
  wait_port session-b-stream "${STREAM_SESSION_B}"
  wait_port session-b-http "${HTTP_SESSION_B}"
}

stop_session() {
  for pid in "${SESSION_B_PID}" "${SESSION_A_PID}"; do
    if [[ -n "${pid}" ]]; then
      for child in $(descendants "${pid}"); do
        kill "${child}" >/dev/null 2>&1 || true
      done
      kill "${pid}" >/dev/null 2>&1 || true
      for _ in $(seq 1 "${PROCESS_STOP_ATTEMPTS}"); do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
          break
        fi
        sleep "${LOCAL_READINESS_POLL_SECONDS}"
      done
      if kill -0 "${pid}" >/dev/null 2>&1; then
        kill -9 "${pid}" >/dev/null 2>&1 || true
      fi
      wait "${pid}" >/dev/null 2>&1 || true
    fi
  done
  SESSION_A_PID=""
  SESSION_B_PID=""
}

start_gateway() {
  read -r GATEWAY_SPOT_PUB GATEWAY_HTTP <<<"$(reserve_gateway_endpoints)"
  "$(role_bin gateway)" \
    --rid gateway \
    --http-url "${GATEWAY_HTTP}" \
    --log-dir "${log_dir}" \
    --evidence-file "${log_dir}/gateway.evidence.log" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --spot-pub-endpoint "${GATEWAY_SPOT_PUB}" \
      >"${log_dir}/gateway.stdout.log" 2>"${log_dir}/gateway.stderr.log" &
  GATEWAY_PID="$!"
  pids+=("$!")
  wait_port gateway-spot-pub "${GATEWAY_SPOT_PUB}"
  wait_port gateway-http "${GATEWAY_HTTP}"
}

stop_gateway() {
  if [[ -z "${GATEWAY_PID}" ]]; then
    return
  fi
  for child in $(descendants "${GATEWAY_PID}"); do
    kill "${child}" >/dev/null 2>&1 || true
  done
  kill "${GATEWAY_PID}" >/dev/null 2>&1 || true
  wait "${GATEWAY_PID}" >/dev/null 2>&1 || true
  GATEWAY_PID=""
}

start_multi_nodes() {
  read -r MULTI_ROUTE_A MULTI_ROUTE_B MULTI_SPOT_A MULTI_SPOT_B MULTI_HTTP_A MULTI_HTTP_B <<<"$(reserve_multinode_endpoints)"
  "$(role_bin multi-node)" \
    --rid multi-node-a \
    --http-url "${MULTI_HTTP_A}" \
    --log-dir "${log_dir}" \
    --evidence-file "${log_dir}/multi-node-a.evidence.log" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --multi-route-a-endpoint "${MULTI_ROUTE_A}" \
    --multi-spot-router-a-endpoint "${MULTI_SPOT_A}" \
      >"${log_dir}/multi-node-a.stdout.log" 2>"${log_dir}/multi-node-a.stderr.log" &
  MULTI_NODE_A_PID="$!"
  pids+=("$!")
  wait_port multi-node-a-route "${MULTI_ROUTE_A}"
  wait_port multi-node-a-spot "${MULTI_SPOT_A}"
  wait_port multi-node-a-http "${MULTI_HTTP_A}"

  "$(role_bin multi-node)" \
    --rid multi-node-b \
    --http-url "${MULTI_HTTP_B}" \
    --log-dir "${log_dir}" \
    --evidence-file "${log_dir}/multi-node-b.evidence.log" \
    --registry-router-endpoint "${REGISTRY_ROUTER}" \
    --multi-route-b-endpoint "${MULTI_ROUTE_B}" \
    --multi-spot-router-b-endpoint "${MULTI_SPOT_B}" \
      >"${log_dir}/multi-node-b.stdout.log" 2>"${log_dir}/multi-node-b.stderr.log" &
  MULTI_NODE_B_PID="$!"
  pids+=("$!")
  wait_port multi-node-b-route "${MULTI_ROUTE_B}"
  wait_port multi-node-b-spot "${MULTI_SPOT_B}"
  wait_port multi-node-b-http "${MULTI_HTTP_B}"
}

stop_multi_nodes() {
  for pid in "${MULTI_NODE_B_PID}" "${MULTI_NODE_A_PID}"; do
    if [[ -n "${pid}" ]]; then
      for child in $(descendants "${pid}"); do
        kill "${child}" >/dev/null 2>&1 || true
      done
      kill "${pid}" >/dev/null 2>&1 || true
      for _ in $(seq 1 "${PROCESS_STOP_ATTEMPTS}"); do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
          break
        fi
        sleep "${LOCAL_READINESS_POLL_SECONDS}"
      done
      if kill -0 "${pid}" >/dev/null 2>&1; then
        kill -9 "${pid}" >/dev/null 2>&1 || true
      fi
      wait "${pid}" >/dev/null 2>&1 || true
    fi
  done
  MULTI_NODE_A_PID=""
  MULTI_NODE_B_PID=""
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

read -r REGISTRY_PUB REGISTRY_ROUTER ROUTE_A ROUTE_B ROUTE_CLIENT SPOT_A SPOT_B SPOT_CLIENT INGRESS_A INGRESS_B SPOT_PUB_A SPOT_PUB_B _ STREAM_A STREAM_B HTTP_A HTTP_B <<<"$(reserve_ports)"

gradle_run clean \
  :Client:installDist \
  :Server:Play:installDist \
  :Server:Gateway:installDist \
  :Server:MultiNode:installDist \
  :Server:Registry:installDist \
  :Server:Session:installDist

start_registry
start_play play-a "${ROUTE_A}" "${SPOT_A}" "${INGRESS_A}" "${HTTP_A}" "${SPOT_PUB_A}" ""
start_play play-b "${ROUTE_B}" "${SPOT_B}" "${INGRESS_B}" "${HTTP_B}" "${SPOT_PUB_B}" ""
sleep "${ROUTE_SETTLE_SECONDS}"

run_client_mode() {
  local mode="$1"
  local route_client
  local spot_client
  local stream_a
  local stream_b
  local attempt
  local status
  for attempt in $(seq 1 5); do
    read -r route_client spot_client <<<"$(reserve_client_endpoints)"
    stream_a="${STREAM_A}"
    stream_b=""
    if [[ "${mode}" == "actor-session" ]]; then
      stream_a="${STREAM_SESSION_A}"
      stream_b="${STREAM_SESSION_B}"
    fi
    set +e
    ZLINK_KOTLIN_E2E_CLIENT_MODE="${mode}" \
      ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT="${route_client}" \
      ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT="${ROUTE_A}" \
      ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT="${ROUTE_B}" \
      ZLINK_KOTLIN_E2E_SPOT_ENDPOINT="${spot_client}" \
      ZLINK_KOTLIN_E2E_SPOT_A_ENDPOINT="${SPOT_A}" \
      ZLINK_KOTLIN_E2E_SPOT_B_ENDPOINT="${SPOT_B}" \
      ZLINK_KOTLIN_E2E_INGRESS_A_ENDPOINT="${INGRESS_A}" \
      ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT="${stream_a}" \
      ZLINK_KOTLIN_E2E_STREAM_B_ENDPOINT="${stream_b}" \
      ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
      ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
      ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT="${HTTP_SESSION_A:-}" \
      ZLINK_KOTLIN_E2E_GATEWAY_HTTP_ENDPOINT="${GATEWAY_HTTP:-}" \
      ZLINK_KOTLIN_E2E_MULTI_HTTP_A_ENDPOINT="${MULTI_HTTP_A:-}" \
      ZLINK_KOTLIN_E2E_MULTI_HTTP_B_ENDPOINT="${MULTI_HTTP_B:-}" \
      ZLINK_KOTLIN_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
      ZLINK_KOTLIN_E2E_LOG_DIR="${log_dir}" \
        timeout -k 5s 75s "$(role_bin client)" >"${log_dir}/client-${mode}.stdout.log" 2>"${log_dir}/client-${mode}.stderr.log"
    status="$?"
    set -e
    if [[ "${status}" == "0" ]] && grep -q "spot-service kotlin e2e mode=${mode} result=passed" "${log_dir}/client-${mode}.stdout.log"; then
      cat "${log_dir}/client-${mode}.stdout.log" >>"${log_dir}/client.stdout.log"
      cat "${log_dir}/client-${mode}.stderr.log" >>"${log_dir}/client.stderr.log"
      return 0
    fi
    (pgrep -f "${client_role_pattern}" 2>/dev/null || true) | while read -r pid; do
      kill -9 "${pid}" >/dev/null 2>&1 || true
    done
    sleep "${MODE_RETRY_SETTLE_SECONDS}"
  done
  return 1
}

scenario_modes() {
  case "$1" in
    all)
      echo "state1 state2 send normal worker missing timeout owner spot-outbound spot-to-spot route-mesh actor-session idle-timer timer-overrun"
      ;;
    SM-A1) echo "state1" ;;
    SM-A2) echo "state2" ;;
    SM-A3|SM-A4) echo "owner" ;;
    SM-A6) echo "lifecycle-close" ;;
    SM-A7) echo "type-mismatch" ;;
    SM-A8) echo "worker" ;;
    SM-B1|SM-B3|SM-B5|SM-B6|SM-B7|SM-B8|SM-D1|SM-D3|SM-D4|SM-D5|SM-D6|SM-D7|SM-D8|SM-D9|SM-D10|SM-D11|SM-D13) echo "actor-session" ;;
    SM-C1) echo "normal" ;;
    SM-C2) echo "spot-outbound" ;;
    SM-C3) echo "spot-to-spot" ;;
    SM-E1) echo "missing" ;;
    SM-E2) echo "timer-basic" ;;
    SM-E3) echo "idle-timer" ;;
    SM-E4) echo "timer-overrun" ;;
    SM-F1|SM-F2|SM-F3|SM-F4) echo "route-mesh" ;;
    *)
      echo "SpotService Kotlin scenario $1 is not mapped to an implemented client mode" >&2
      return 1
      ;;
  esac
}

: >"${log_dir}/client.stdout.log"
: >"${log_dir}/client.stderr.log"
client_modes="${ZLINK_KOTLIN_E2E_MODES:-$(scenario_modes "${SCENARIO}")}"
for mode in ${client_modes}; do
  if [[ "${mode}" == "idle-timer" ]]; then
    create_timer_spot "${HTTP_A}" idle-close
    create_timer_spot "${HTTP_A}" idle-active
    sleep "${ROUTE_SETTLE_SECONDS}"
  fi
  if [[ "${mode}" == "timer-overrun" ]]; then
    create_timer_spot "${HTTP_A}" timer-overrun-skip
    create_timer_spot "${HTTP_A}" timer-overrun-catchup
    create_timer_spot "${HTTP_A}" timer-overrun-delay
    sleep "${ROUTE_SETTLE_SECONDS}"
  fi
  if [[ "${mode}" == "actor-session" ]]; then
    start_session
    sleep "${MODE_RETRY_SETTLE_SECONDS}"
  fi
  run_client_mode "${mode}"
  if [[ "${mode}" == "actor-session" ]]; then
    fetch_evidence session-a "${HTTP_SESSION_A}"
    fetch_evidence session-b "${HTTP_SESSION_B}"
    stop_session
  fi
  if [[ "${mode}" == "idle-timer" ]]; then
    close_spot "${HTTP_A}" idle-active
  fi
  sleep "${ROUTE_SETTLE_SECONDS}"
done
if [[ -n "${ZLINK_KOTLIN_E2E_MODES:-}" || "${SCENARIO}" != "all" ]]; then
  cat "${log_dir}/client.stdout.log"
  fetch_evidence play-a "${HTTP_A}"
  fetch_evidence play-b "${HTTP_B}"
  echo "spot-service kotlin e2e focused modes=${client_modes} result=passed"
  exit 0
fi
start_gateway
sleep "${MODE_RETRY_SETTLE_SECONDS}"
run_client_mode gateway-publish
fetch_evidence gateway "${GATEWAY_HTTP}"
stop_gateway
run_client_mode type-mismatch
run_client_mode timer-basic
run_client_mode lifecycle-close
start_multi_nodes
sleep "${MODE_RETRY_SETTLE_SECONDS}"
run_client_mode multi-node
fetch_evidence multi-node-a "${MULTI_HTTP_A}"
fetch_evidence multi-node-b "${MULTI_HTTP_B}"
stop_multi_nodes

cat "${log_dir}/client.stdout.log"
fetch_evidence play-a "${HTTP_A}"
fetch_evidence play-b "${HTTP_B}"
grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q "packet=RoutePingReq" "${log_dir}/play-a-flow.log"
grep -q '"marker":"RoutePingReq"' "${log_dir}/play-a-evidence.json"
grep -q '"value":"route-mesh-normal"' "${log_dir}/play-a-evidence.json"
grep -q '"marker":"ActorCreated"' "${log_dir}/session-a-evidence.json"
grep -q '"marker":"ActorCreatedPayload"' "${log_dir}/session-a-evidence.json"
grep -q '"value":"Player One/7/alpha,beta"' "${log_dir}/session-a-evidence.json"
grep -q '"marker":"ActorUserJoined"' "${log_dir}/session-a-evidence.json"
grep -q '"marker":"ActorUserRequest"' "${log_dir}/session-a-evidence.json"
grep -q 'ActorCreated.*ActorUserJoined.*ActorUserRequest' "${log_dir}/session-a-evidence.json"
grep -q 'user-echo-1.*user-echo-2.*user-echo-3' "${log_dir}/session-a-evidence.json"
if grep -q '"marker":"ActorCreated"' "${log_dir}/play-b-evidence.json"; then
  echo "unexpected play-b actor creation evidence" >&2
  exit 1
fi
if grep -q '"marker":"ActorUserJoined"' "${log_dir}/play-b-evidence.json"; then
  echo "unexpected play-b actor join evidence" >&2
  exit 1
fi
grep -q '"marker":"StreamInbound"' "${log_dir}/session-a-evidence.json"
grep -q "DispatchError" "${log_dir}/play-a-evidence.json"
grep -q "SpotInitialized" "${log_dir}/play-a-evidence.json"
grep -q "SpotClosing" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
grep -q "SpotTypeMismatch" "${log_dir}/play-a-evidence.json"
grep -q "SpotTypeMismatchStateOk" "${log_dir}/play-a-evidence.json"
grep -q "SpotTimer" "${log_dir}/play-a-evidence.json"
grep -q "WorkerStarted" "${log_dir}/play-a-evidence.json"
grep -q "WorkerFollowUpBeforeComplete" "${log_dir}/play-a-evidence.json"
grep -q "WorkerCompleted" "${log_dir}/play-a-evidence.json"
grep -q "IngressRequest" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
grep -q "IngressCommand" "${log_dir}/play-a-evidence.json" "${log_dir}/play-b-evidence.json"
grep -q "SpotOutbound" "${log_dir}/play-a-evidence.json"
grep -q "SpotToSpotSend" "${log_dir}/play-b-evidence.json"
grep -q "SpotMeshEvent" "${log_dir}/play-a-evidence.json"
grep -q "SpotMeshEvent" "${log_dir}/play-b-evidence.json"
grep -q "spot-publish|rid=gateway" "${log_dir}/gateway-evidence.json"
grep -q "IdleCloseRequested" "${log_dir}/play-a-evidence.json"
grep -q "IdleClosed" "${log_dir}/play-a-evidence.json"
grep -q "IdleKeptOpen" "${log_dir}/play-a-evidence.json"
grep -q "TimerOverrunConfigured" "${log_dir}/play-a-evidence.json"
grep -q "TimerOverrunTick" "${log_dir}/play-a-evidence.json"
grep -q "multi-state-request|node=multi-node-a" "${log_dir}/multi-node-a-evidence.json"
grep -q "multi-state-request|node=multi-node-b" "${log_dir}/multi-node-b-evidence.json"
echo "spot-service kotlin e2e result=passed"
