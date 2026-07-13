#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/e2e-redis-common.sh"

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
redis_container_name=""
redis_proxy_pid=""
store_pause_command=""
store_resume_command=""
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
export ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR:-${HOME}/.cache/zlink/java-e2e/ResilienceLifecycle}"
export ZLINK_JAVA_E2E_GRADLE_CACHE="${ZLINK_JAVA_E2E_GRADLE_CACHE:-${HOME}/.cache/zlink/java-e2e/ResilienceLifecycle-gradle-cache}"
export ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX:-zlink:e2e:resilience-lifecycle:${run_id}}"
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
  if [[ -n "${redis_container_name}" ]]; then
    docker rm -fv "${redis_container_name}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${redis_proxy_pid}" ]]; then
    kill -CONT "${redis_proxy_pid}" >/dev/null 2>&1 || true
    kill "${redis_proxy_pid}" >/dev/null 2>&1 || true
    sleep 0.2
    kill -9 "${redis_proxy_pid}" >/dev/null 2>&1 || true
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
    for _ in range(8):
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

reserve_tcp_port() {
  python3 - <<'PY'
import socket
sock = socket.socket()
try:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
finally:
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

gradle_run() {
  ../../gradlew --project-cache-dir "${ZLINK_JAVA_E2E_GRADLE_CACHE}" --no-daemon "$@" --quiet
}

client_bin() {
  echo "${ZLINK_JAVA_E2E_BUILD_DIR}/Client/install/resilience-lifecycle-client/bin/resilience-lifecycle-client"
}

start_redis_proxy() {
  if [[ -z "${explicit_redis_endpoint}" || !( "${SCENARIO}" == "all" || "${SCENARIO}" == "RL-C4" || "${SCENARIO}" == "rl-c4" ) ]]; then
    return
  fi
  local proxy_port
  proxy_port="$(reserve_tcp_port)"
  python3 - "${explicit_redis_endpoint}" "${proxy_port}" >"${log_dir}/redis-proxy.stdout.log" 2>"${log_dir}/redis-proxy.stderr.log" <<'PY' &
import selectors
import socket
import sys
import threading

target = sys.argv[1]
listen_port = int(sys.argv[2])
if target.startswith("tcp://"):
    target = target[len("tcp://"):]
host, port_text = target.rsplit(":", 1)
target_addr = (host, int(port_text))

def pump(source, destination):
    try:
        while True:
            data = source.recv(65536)
            if not data:
                return
            destination.sendall(data)
    except OSError:
        return
    finally:
        for sock in (source, destination):
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", listen_port))
    server.listen()
    print(f"redis proxy listening on 127.0.0.1:{listen_port} -> {target_addr[0]}:{target_addr[1]}", flush=True)
    while True:
        client, _ = server.accept()
        try:
            upstream = socket.create_connection(target_addr, timeout=5)
        except OSError:
            client.close()
            continue
        threading.Thread(target=pump, args=(client, upstream), daemon=True).start()
        threading.Thread(target=pump, args=(upstream, client), daemon=True).start()
PY
  redis_proxy_pid="$!"
  ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="127.0.0.1:${proxy_port}"
  export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT
  wait_port redis-proxy "127.0.0.1:${proxy_port}"
}

create_store_outage_commands() {
  store_pause_command="${log_dir}/store-pause"
  store_resume_command="${log_dir}/store-resume"
  if [[ -n "${redis_container_name}" ]]; then
    cat >"${store_pause_command}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
docker pause "${redis_container_name}" >/dev/null
EOF
    cat >"${store_resume_command}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
docker unpause "${redis_container_name}" >/dev/null
EOF
  elif [[ -n "${redis_proxy_pid}" ]]; then
    cat >"${store_pause_command}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
kill -STOP "${redis_proxy_pid}"
EOF
    cat >"${store_resume_command}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
kill -CONT "${redis_proxy_pid}"
EOF
  else
    cat >"${store_pause_command}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
echo "RL-C4 requires a Redis container or Redis proxy started by this runner." >&2
exit 1
EOF
    cat >"${store_resume_command}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
echo "RL-C4 requires a Redis container or Redis proxy started by this runner." >&2
exit 1
EOF
  fi
  chmod +x "${store_pause_command}" "${store_resume_command}"
}

explicit_redis_endpoint=""
zlink_redis_start_scoped_assign redis_container_name redis_port \
  "zlink-redis-java-e2e" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}" "127.0.0.1::6379"
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="127.0.0.1:${redis_port}"
start_redis_proxy
create_store_outage_commands

read -r API_A API_B API_A_REPLACEMENT API_B_GREEN HTTP_A HTTP_B HTTP_A_REPLACEMENT HTTP_B_GREEN <<<"$(reserve_ports)"

gradle_run installDist

ZLINK_JAVA_E2E_CLIENT_MODE="suite" \
ZLINK_JAVA_E2E_SCENARIO="${SCENARIO}" \
ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT="${ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT}" \
ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX="${ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX}" \
ZLINK_JAVA_E2E_STORE_PAUSE_COMMAND="${store_pause_command}" \
ZLINK_JAVA_E2E_STORE_RESUME_COMMAND="${store_resume_command}" \
ZLINK_JAVA_E2E_API_A_ENDPOINT="${API_A}" \
ZLINK_JAVA_E2E_API_B_ENDPOINT="${API_B}" \
ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT="${API_A_REPLACEMENT}" \
ZLINK_JAVA_E2E_API_B_GREEN_ENDPOINT="${API_B_GREEN}" \
ZLINK_JAVA_E2E_HTTP_A_ENDPOINT="${HTTP_A}" \
ZLINK_JAVA_E2E_HTTP_B_ENDPOINT="${HTTP_B}" \
ZLINK_JAVA_E2E_HTTP_A_REPLACEMENT_ENDPOINT="${HTTP_A_REPLACEMENT}" \
ZLINK_JAVA_E2E_HTTP_B_GREEN_ENDPOINT="${HTTP_B_GREEN}" \
ZLINK_JAVA_E2E_BUILD_DIR="${ZLINK_JAVA_E2E_BUILD_DIR}" \
ZLINK_JAVA_E2E_LOG_DIR="${log_dir}" \
  "$(client_bin)" >"${log_dir}/client.stdout.log" 2>"${log_dir}/client.stderr.log"

cat "${log_dir}/client.stdout.log"
cat "${log_dir}"/consumer-*.stdout.log
if [[ "${SCENARIO}" == "all" ]]; then
  grep -q "scenario RL-A1 passed" "${log_dir}/consumer-restart.stdout.log"
  grep -q "scenario RL-A2 passed" "${log_dir}/consumer-reschedule.stdout.log"
  grep -q "scenario RL-A3 passed" "${log_dir}"/consumer-storm-*.stdout.log
  grep -q "scenario RL-A4 passed" "${log_dir}/consumer-rolling-green.stdout.log"
  grep -q "scenario RL-A5 passed" "${log_dir}/consumer-flapping.stdout.log"
  grep -q "scenario RL-B1 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B2 passed" "${log_dir}/consumer-crash-inflight.stdout.log"
  grep -q "scenario RL-B3 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B4 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B5 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-B6 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-C1 passed" "${log_dir}/consumer-cleanup.stdout.log"
  grep -q "scenario RL-C2 passed" "${log_dir}/consumer-topology-recovery.stdout.log"
  grep -q "scenario RL-C3 passed" "${log_dir}/consumer-restart.stdout.log"
  grep -q "scenario RL-D1 passed" "${log_dir}"/consumer-storm-*.stdout.log
  grep -q "scenario RL-D2 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-D3 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-D4 passed" "${log_dir}/consumer-default.stdout.log"
  grep -q "scenario RL-D5 passed" "${log_dir}/consumer-cleanup.stdout.log"
else
  grep -Rq "scenario ${SCENARIO} passed" "${log_dir}"/consumer-*.stdout.log
fi
grep -q "resilience-lifecycle e2e result=passed" "${log_dir}/client.stdout.log"
grep -Rq "message flow" "${log_dir}"/*-flow.log
