#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
role_pattern='systems\.zlink\.e2e\.yielddispatch\.(client|delay|play|registry|session)\.Program'
run_id="$(date +%Y%m%d-%H%M%S)-$$"
log_dir="$(pwd)/logs/${run_id}"
repo_root="$(cd ../../../../.. && pwd)"
default_core_lib="${repo_root}/core/build/lib/libzlink.so"
mkdir -p "${log_dir}"
echo "log_dir=${log_dir}"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" && -f "${default_core_lib}" ]]; then
  export ZLINK_LIBRARY_PATH="${default_core_lib}"
fi
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/YieldDispatch}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/YieldDispatch-gradle-cache}"

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
    for _ in range(13):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
        ports.append(sock.getsockname()[1])
    print(" ".join(str(port) for port in ports))
finally:
    for sock in sockets:
        sock.close()
PY
}

tcp() {
  echo "tcp://127.0.0.1:$1"
}

http() {
  echo "http://127.0.0.1:$1"
}

port_of() {
  echo "${1##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "${endpoint}")"
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_http() {
  local name="$1"
  local endpoint="$2"
  for _ in $(seq 1 600); do
    if python3 - "${endpoint}/evidence" >/dev/null 2>&1 <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=1) as response:
    response.read()
PY
    then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_evidence_contains() {
  local endpoint="$1"
  local marker="$2"
  local subject="$3"
  local message="$4"
  for _ in $(seq 1 600); do
    if python3 - "${endpoint}/evidence" "${marker}" "${subject}" >/dev/null 2>&1 <<'PY'
import json
import sys
import urllib.request

with urllib.request.urlopen(sys.argv[1], timeout=1) as response:
    snapshot = json.loads(response.read().decode("utf-8"))
for entry in snapshot.get("entries", []):
    if entry.get("marker") == sys.argv[2] and entry.get("subject") == sys.argv[3]:
        sys.exit(0)
sys.exit(1)
PY
    then
      return 0
    fi
    sleep 0.1
  done
  echo "${message}" >&2
  return 1
}

fetch_evidence() {
  local endpoint="$1"
  local output="$2"
  python3 - "${endpoint}/evidence" >"${output}" <<'PY'
import sys
import urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as response:
    sys.stdout.write(response.read().decode("utf-8"))
PY
}

write_marker_report() {
  python3 - "${log_dir}/yield-dispatch-marker-report.json" \
    "${log_dir}/play-a-evidence.json" \
    "${log_dir}/play-b-evidence.json" \
    "${log_dir}/session-evidence.json" <<'PY'
import json
import sys

output = sys.argv[1]
sources = {
    "play-a": sys.argv[2],
    "play-b": sys.argv[3],
    "session": sys.argv[4],
}
scenario_prefixes = {
    "YD-A1": ["yda1-"],
    "YD-A2": ["yda2-"],
    "YD-A3": ["yda3-"],
    "YD-A4": ["yda4-"],
    "YD-B1": ["ydb1-"],
    "YD-B2": ["ydb2-"],
    "YD-B3": ["ydb3-"],
    "YD-C1": ["ydc1-"],
    "YD-C2": ["ydc2-"],
    "YD-C3": ["ydc3-"],
    "YD-D2": ["ydd2-"],
    "YD-D3": ["ydd3-"],
    "YD-D4": ["ydd4-"],
    "YD-E1": ["yde1-"],
}
required_markers = {
    "YD-A1": ["hold-started", "hold-resumed", "hold-completed", "probe-started"],
    "YD-A2": ["yield-started", "yield-released", "probe-started", "probe-completed", "yield-resumed", "yield-completed"],
    "YD-A3": ["yield-started", "yield-released", "yield-resumed", "yield-completed"],
    "YD-A4": ["worker-yield-started", "worker-yield-released", "probe-started", "probe-completed", "worker-yield-resumed", "worker-yield-completed"],
    "YD-B1": ["actor-yield-started", "actor-yield-released", "actor-fast-started", "actor-fast-completed", "actor-yield-resumed", "actor-yield-completed"],
    "YD-B2": ["actor-yield-started", "actor-yield-released", "actor-yield-resumed", "actor-yield-completed", "actor-fast-started", "actor-fast-completed"],
    "YD-B3": ["actor-join-yield-started", "actor-join-yield-released", "actor-fast-started", "actor-fast-completed", "actor-join-yield-resumed", "actor-join-yield-completed"],
    "YD-C1": ["timer-yield-started", "timer-yield-released", "timer-fast-started", "timer-fast-completed", "timer-yield-resumed", "timer-yield-completed"],
    "YD-C2": ["timer-yield-started", "timer-yield-released", "timer-yield-resumed", "timer-yield-completed", "timer-next-started", "timer-next-completed"],
    "YD-C3": ["actor-yield-started", "actor-yield-released", "timer-fast-started", "timer-fast-completed", "actor-yield-resumed", "actor-yield-completed", "timer-yield-started", "timer-yield-released", "actor-fast-started", "actor-fast-completed", "timer-yield-resumed", "timer-yield-completed"],
    "YD-D2": ["remote-yield-started", "remote-yield-released", "remote-yield-resumed", "remote-yield-completed", "yield-started", "yield-released", "yield-resumed", "yield-completed"],
    "YD-D3": ["yield-started", "yield-released", "probe-started", "probe-completed", "yield-resumed", "yield-completed"],
    "YD-D4": ["actor-push-yield-started", "actor-push-yield-released", "actor-push-yield-resumed", "actor-push-yield-completed"],
    "YD-E1": ["timeout-yield-started", "timeout-yield-released", "timeout-yield-completed", "probe-started", "probe-completed"],
}

entries = []
for role, path in sources.items():
    with open(path, encoding="utf-8") as handle:
        snapshot = json.load(handle)
    for entry in snapshot.get("entries", []):
        entries.append({
            "role": role,
            "marker": entry.get("marker", ""),
            "subject": entry.get("subject", ""),
            "value": entry.get("value", ""),
        })

scenarios = {}
for scenario_id, prefixes in scenario_prefixes.items():
    scenario_entries = [
        entry for entry in entries
        if any(entry["subject"].startswith(prefix) for prefix in prefixes)
    ]
    observed = sorted({entry["marker"] for entry in scenario_entries})
    missing = [
        marker for marker in required_markers[scenario_id]
        if marker not in observed
    ]
    if missing:
        raise SystemExit(f"{scenario_id} marker report missing markers: {', '.join(missing)}")
    scenarios[scenario_id] = {
        "markers": observed,
        "roles": sorted({entry["role"] for entry in scenario_entries}),
    }

with open(output, "w", encoding="utf-8") as handle:
    json.dump({
        "language": "java",
        "config": "YieldDispatch",
        "scenarios": scenarios,
    }, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
PY
}

terminate_gracefully() {
  local name="$1"
  local pid="$2"
  local targets=()
  local child
  while read -r child; do
    [[ -n "${child}" ]] && targets+=("${child}")
  done < <(descendants "${pid}")
  targets+=("${pid}")
  for child in "${targets[@]}"; do
    kill -TERM "${child}" >/dev/null 2>&1 || true
  done
  for _ in $(seq 1 600); do
    local alive=0
    for child in "${targets[@]}"; do
      if kill -0 "${child}" >/dev/null 2>&1; then
        alive=1
        break
      fi
    done
    if [[ "${alive}" == "0" ]]; then
      wait "${pid}" >/dev/null 2>&1 || true
      return 0
    fi
    sleep 0.1
  done
  echo "${name} did not stop after SIGTERM; sending SIGKILL" >&2
  for child in "${targets[@]}"; do
    if command -v jcmd >/dev/null 2>&1 && kill -0 "${child}" >/dev/null 2>&1; then
      jcmd "${child}" Thread.print >"${log_dir}/${name}-${child}-thread-dump.log" 2>&1 || true
    fi
  done
  for child in "${targets[@]}"; do
    kill -KILL "${child}" >/dev/null 2>&1 || true
  done
  wait "${pid}" >/dev/null 2>&1 || true
  return 1
}

static_checks() {
  local tmp
  tmp="$(mktemp)"
  if grep -RInE 'POST|/yield|\.POST\(' Client Server Shared --include='*.java' >"${tmp}"; then
    cat "${tmp}" >&2
    rm -f "${tmp}"
    echo "YieldDispatch scenarios must not be triggered through HTTP." >&2
    return 1
  fi
  rm -f "${tmp}"

  tmp="$(mktemp)"
  if grep -RInF '.yield(' . --include='*.java' \
      | grep -v 'Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/YieldProbeHandlers.java' >"${tmp}"; then
    cat "${tmp}" >&2
    rm -f "${tmp}"
    echo "YieldDispatch may only call .yield from Play Spot/Entry Spot handlers." >&2
    return 1
  fi
  rm -f "${tmp}"

  if ! grep -q 'ZLinkStreamConnectorFactory.create' Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java; then
    echo "YieldDispatch client must create a real stream connector directly." >&2
    return 1
  fi
}

wait_readiness() {
  : >"${log_dir}/readiness.stdout.log"
  : >"${log_dir}/readiness.stderr.log"
  for attempt in $(seq 1 60); do
    if ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
      ZLINK_JAVA_E2E_PLAY_HTTP="${PLAY_A_HTTP}" \
      ZLINK_JAVA_E2E_PLAY_B_HTTP="${PLAY_B_HTTP}" \
      ZLINK_JAVA_E2E_SESSION_HTTP="${SESSION_HTTP}" \
      ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
        timeout -k 5s 20s "$(client_bin)" --readiness \
        >>"${log_dir}/readiness.stdout.log" 2>>"${log_dir}/readiness.stderr.log"; then
      return 0
    fi
    printf 'readiness attempt %s failed\n' "${attempt}" >>"${log_dir}/readiness.stderr.log"
    sleep 0.5
  done
  echo "Timed out waiting for route and spot readiness" >&2
  return 1
}

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon --no-parallel --max-workers=1 "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/yield-dispatch-client/bin/yield-dispatch-client"
}

registry_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Registry/install/yield-dispatch-registry/bin/yield-dispatch-registry"
}

delay_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Delay/install/yield-dispatch-delay/bin/yield-dispatch-delay"
}

play_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Play/install/yield-dispatch-play/bin/yield-dispatch-play"
}

session_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Server-Session/install/yield-dispatch-session/bin/yield-dispatch-session"
}

read -r REG_PUB_PORT REG_ROUTER_PORT DELAY_PORT ROUTE_A_PORT SPOT_A_PORT ROUTE_B_PORT SPOT_B_PORT STREAM_PORT PLAY_A_HTTP_PORT PLAY_B_HTTP_PORT SESSION_HTTP_PORT SESSION_ROUTE_PORT SESSION_SPOT_PORT <<<"$(reserve_ports)"
REGISTRY_PUB="$(tcp "${REG_PUB_PORT}")"
REGISTRY_ROUTER="$(tcp "${REG_ROUTER_PORT}")"
DELAY_ENDPOINT="$(tcp "${DELAY_PORT}")"
ROUTE_A_ENDPOINT="$(tcp "${ROUTE_A_PORT}")"
SPOT_A_ENDPOINT="$(tcp "${SPOT_A_PORT}")"
ROUTE_B_ENDPOINT="$(tcp "${ROUTE_B_PORT}")"
SPOT_B_ENDPOINT="$(tcp "${SPOT_B_PORT}")"
STREAM_ENDPOINT="$(tcp "${STREAM_PORT}")"
PLAY_A_HTTP="$(http "${PLAY_A_HTTP_PORT}")"
PLAY_B_HTTP="$(http "${PLAY_B_HTTP_PORT}")"
SESSION_HTTP="$(http "${SESSION_HTTP_PORT}")"
SESSION_ROUTE_ENDPOINT="$(tcp "${SESSION_ROUTE_PORT}")"
SESSION_SPOT_ENDPOINT="$(tcp "${SESSION_SPOT_PORT}")"

static_checks
gradle_run installDist

ZLINK_JAVA_E2E_REGISTRY_PUB="${REGISTRY_PUB}" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(registry_bin)" >"${log_dir}/registry.stdout.log" 2>"${log_dir}/registry.stderr.log" &
pids+=("$!")
wait_port registry-router "${REGISTRY_ROUTER}"

ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(delay_bin)" >"${log_dir}/delay.stdout.log" 2>"${log_dir}/delay.stderr.log" &
pids+=("$!")
wait_port delay "${DELAY_ENDPOINT}"

ZLINK_JAVA_E2E_NODE_RID="play-a" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_A_ENDPOINT}" \
ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT="${ROUTE_B_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_A_ENDPOINT}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${PLAY_A_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(play_bin)" >"${log_dir}/play-a.stdout.log" 2>"${log_dir}/play-a.stderr.log" &
pids+=("$!")
wait_port play-a-route "${ROUTE_A_ENDPOINT}"
wait_port play-a-spot "${SPOT_A_ENDPOINT}"
wait_http play-a-http "${PLAY_A_HTTP}"

ZLINK_JAVA_E2E_NODE_RID="play-b" \
ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_B_ENDPOINT}" \
ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT="${ROUTE_A_ENDPOINT}" \
ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_B_ENDPOINT}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${PLAY_B_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(play_bin)" >"${log_dir}/play-b.stdout.log" 2>"${log_dir}/play-b.stderr.log" &
pids+=("$!")
wait_port play-b-route "${ROUTE_B_ENDPOINT}"
wait_port play-b-spot "${SPOT_B_ENDPOINT}"
wait_http play-b-http "${PLAY_B_HTTP}"

ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_A_ENDPOINT}" \
ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT="${ROUTE_B_ENDPOINT}" \
ZLINK_JAVA_E2E_SESSION_ROUTE_ENDPOINT="${SESSION_ROUTE_ENDPOINT}" \
ZLINK_JAVA_E2E_SESSION_SPOT_ENDPOINT="${SESSION_SPOT_ENDPOINT}" \
ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
ZLINK_JAVA_E2E_HTTP_ENDPOINT="${SESSION_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(session_bin)" >"${log_dir}/session.stdout.log" 2>"${log_dir}/session.stderr.log" &
pids+=("$!")
wait_port session-route "${SESSION_ROUTE_ENDPOINT}"
wait_port session-spot "${SESSION_SPOT_ENDPOINT}"
wait_port session-stream "${STREAM_ENDPOINT}"
wait_http session-http "${SESSION_HTTP}"
wait_readiness

ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
ZLINK_JAVA_E2E_PLAY_HTTP="${PLAY_A_HTTP}" \
ZLINK_JAVA_E2E_PLAY_B_HTTP="${PLAY_B_HTTP}" \
ZLINK_JAVA_E2E_SESSION_HTTP="${SESSION_HTTP}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  timeout -k 5s 90s "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

fetch_evidence "${PLAY_A_HTTP}" "${log_dir}/play-a-evidence.json"
fetch_evidence "${PLAY_B_HTTP}" "${log_dir}/play-b-evidence.json"
fetch_evidence "${SESSION_HTTP}" "${log_dir}/session-evidence.json"
write_marker_report
cat "${log_dir}/client.stdout.log"
grep -q "scenario YD-A1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-A2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-A3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-A4 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-B1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-B2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-B3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-C1 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-C2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-C3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-D2 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-D3 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-D4 passed" "${log_dir}/client.stdout.log"
grep -q "scenario YD-E1 passed" "${log_dir}/client.stdout.log"
grep -q "yield-dispatch e2e result=passed" "${log_dir}/client.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
grep -q '"YD-E1"' "${log_dir}/yield-dispatch-marker-report.json"
echo "yield-dispatch marker report=${log_dir}/yield-dispatch-marker-report.json"

if [[ "${ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN:-0}" == "1" ]]; then
  SHUTDOWN_ID="yde3-$(date +%s)-$$"
  SHUTDOWN_SPOT="yield-shutdown-${run_id//[^a-zA-Z0-9]/}"
  ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
  ZLINK_JAVA_E2E_PLAY_HTTP="${PLAY_A_HTTP}" \
  ZLINK_JAVA_E2E_PLAY_B_HTTP="${PLAY_B_HTTP}" \
  ZLINK_JAVA_E2E_SESSION_HTTP="${SESSION_HTTP}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  ZLINK_JAVA_E2E_SHUTDOWN_REQUEST_ID="${SHUTDOWN_ID}" \
  ZLINK_JAVA_E2E_SHUTDOWN_SPOT_RID="${SHUTDOWN_SPOT}" \
    timeout -k 5s 120s "$(client_bin)" --shutdown-wait \
      >"${log_dir}/client-shutdown-wait.stdout.log" 2>"${log_dir}/client-shutdown-wait.stderr.log" &
  SHUTDOWN_CLIENT_PID=$!
  wait_evidence_contains \
    "${PLAY_A_HTTP}" \
    "yield-released" \
    "${SHUTDOWN_ID}" \
    "YD-E3 pending yield marker was not observed before shutdown."
  fetch_evidence "${PLAY_A_HTTP}" "${log_dir}/play-a-shutdown-before-stop-evidence.json"
  terminate_gracefully play-a "${pids[2]}"
  wait "${SHUTDOWN_CLIENT_PID}"
  cat "${log_dir}/client-shutdown-wait.stdout.log"
  grep -q "yield-dispatch shutdown wait result=passed" "${log_dir}/client-shutdown-wait.stdout.log"

  ZLINK_JAVA_E2E_NODE_RID="play-a" \
  ZLINK_JAVA_E2E_REGISTRY_ROUTER="${REGISTRY_ROUTER}" \
  ZLINK_JAVA_E2E_ROUTE_ENDPOINT="${ROUTE_A_ENDPOINT}" \
  ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT="${ROUTE_B_ENDPOINT}" \
  ZLINK_JAVA_E2E_SPOT_ENDPOINT="${SPOT_A_ENDPOINT}" \
  ZLINK_JAVA_E2E_DELAY_ENDPOINT="${DELAY_ENDPOINT}" \
  ZLINK_JAVA_E2E_HTTP_ENDPOINT="${PLAY_A_HTTP}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
    "$(play_bin)" >"${log_dir}/play-a-restart.stdout.log" 2>"${log_dir}/play-a-restart.stderr.log" &
  pids+=("$!")
  wait_port play-a-route "${ROUTE_A_ENDPOINT}"
  wait_port play-a-spot "${SPOT_A_ENDPOINT}"
  wait_http play-a-http "${PLAY_A_HTTP}"
  wait_readiness

  ZLINK_JAVA_E2E_STREAM_ENDPOINT="${STREAM_ENDPOINT}" \
  ZLINK_JAVA_E2E_PLAY_HTTP="${PLAY_A_HTTP}" \
  ZLINK_JAVA_E2E_PLAY_B_HTTP="${PLAY_B_HTTP}" \
  ZLINK_JAVA_E2E_SESSION_HTTP="${SESSION_HTTP}" \
  ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  ZLINK_JAVA_E2E_SHUTDOWN_REQUEST_ID="${SHUTDOWN_ID}" \
  ZLINK_JAVA_E2E_SHUTDOWN_SPOT_RID="${SHUTDOWN_SPOT}" \
    timeout -k 5s 90s "$(client_bin)" --shutdown-recovery \
      >"${log_dir}/client-shutdown-recovery.stdout.log" 2>"${log_dir}/client-shutdown-recovery.stderr.log"
  cat "${log_dir}/client-shutdown-recovery.stdout.log"
  grep -q "yield-dispatch shutdown recovery result=passed" "${log_dir}/client-shutdown-recovery.stdout.log"
  fetch_evidence "${PLAY_A_HTTP}" "${log_dir}/play-a-restart-evidence.json"
  grep -q "yield-dispatch shutdown recovery result=passed" "${log_dir}/client-shutdown-recovery.stdout.log"
  echo "scenario YD-E3 passed"
fi
