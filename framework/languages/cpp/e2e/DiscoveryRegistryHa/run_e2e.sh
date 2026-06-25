#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_discovery_registry_ha_registry \
  zlink_cpp_e2e_discovery_registry_ha_client \
  zlink_cpp_e2e_registry_messaging_server >/dev/null

REGISTRY="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_registry"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_discovery_registry_ha_client"
PROVIDER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_server"
PIDS=()
LAST_PID=""

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
  exit "$code"
}
trap cleanup EXIT

ports() {
  python3 - "$1" <<'PY'
import socket, sys
count = int(sys.argv[1])
sockets = []
ports = []
for _ in range(count):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(str(p) for p in ports))
for s in sockets:
    s.close()
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
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 120); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
  fi
  wait "$pid" >/dev/null 2>&1 || true
}

start_registry() {
  local name="$1"
  local pub="$2"
  local router="$3"
  local http="$4"
  local peers="$5"
  ZLINK_CPP_E2E_REGISTRY_PUB="$pub" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$router" \
  ZLINK_CPP_E2E_REGISTRY_PEERS="$peers" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR/$name" \
    "$REGISTRY" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$name-router" "$router"
  wait_port "$name-http" "$http"
}

start_provider() {
  local rid="$1"
  local endpoint="$2"
  local registry_router="$3"
  ZLINK_CPP_E2E_ROLE=provider \
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$rid" \
  ZLINK_CPP_E2E_API_ENDPOINT="$endpoint" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR/$rid" \
    "$PROVIDER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-api" "$endpoint"
}

start_provider_instance() {
  local rid="$1"
  local instance="$2"
  local endpoint="$3"
  local registry_router="$4"
  ZLINK_CPP_E2E_ROLE=provider \
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$instance" \
  ZLINK_CPP_E2E_API_ENDPOINT="$endpoint" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR/$instance" \
    "$PROVIDER" >"$LOG_DIR/$instance.stdout.log" 2>"$LOG_DIR/$instance.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$instance-api" "$endpoint"
}

start_embedded_provider() {
  local rid="$1"
  local endpoint="$2"
  local registry_pub="$3"
  local registry_router="$4"
  local http="$5"
  ZLINK_CPP_E2E_ROLE=provider \
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$rid" \
  ZLINK_CPP_E2E_API_ENDPOINT="$endpoint" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_EMBEDDED_REGISTRY_PUB="$registry_pub" \
  ZLINK_CPP_E2E_EMBEDDED_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR/$rid" \
    "$PROVIDER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-api" "$endpoint"
  wait_port "$rid-registry-router" "$registry_router"
  wait_port "$rid-http" "$http"
}

run_client() {
  local name="$1"
  local registry_router="$2"
  local expected="$3"
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_E2E_EXPECT_PROVIDERS="$expected" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR/$name" \
    "$CLIENT" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log"
}

wait_member_peers() {
  local http="$1"
  local expected="$2"
  python3 - "$http" "$expected" <<'PY'
import json, sys, time, urllib.request
endpoint, expected = sys.argv[1], int(sys.argv[2])
deadline = time.time() + 20
last = None
while time.time() < deadline:
    try:
        with urllib.request.urlopen(endpoint + "/evidence", timeout=2) as response:
            data = json.loads(response.read().decode("utf-8"))
        last = data
        if len(data.get("member_peers", [])) >= expected:
            sys.exit(0)
    except Exception as exc:
        last = str(exc)
    time.sleep(0.2)
print(f"timed out waiting for {expected} member peers from {endpoint}: {last}", file=sys.stderr)
sys.exit(1)
PY
}

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT <<<"$(ports 7)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"
R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"
R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"
R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"
R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_registry dr-a1-reg "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" ""
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER"
wait_member_peers "$R1_HTTP" 1
run_client dr-a1-client "$R1_ROUTER" "api-a"
echo "scenario DR-A1 passed"
TOPOLOGY_KEYS="$(python3 - "$R1_HTTP/evidence" <<'PY'
import json, sys, time, urllib.request
url = sys.argv[1]
role_map = {
    "router": "server",
    "dealer": "client",
    "pub": "publisher",
    "sub": "subscriber",
    "spot": "spot_node",
    "invalid": "server",
}
deadline = time.time() + 5
while time.time() < deadline:
    with urllib.request.urlopen(url, timeout=2) as response:
        data = json.loads(response.read().decode("utf-8"))
    keys = sorted(
        f"{entry.get('channel','')}|{role_map.get(entry.get('role',''), entry.get('role',''))}|{entry.get('endpoint','')}|{entry.get('routing_id','')}"
        for entry in data.get("topology", [])
    )
    if keys:
        print("\n".join(keys))
        raise SystemExit(0)
    time.sleep(0.05)
raise SystemExit("registry topology was empty")
PY
)"
ZLINK_CPP_E2E_REGISTRY_ROUTER="$R1_ROUTER" \
ZLINK_CPP_E2E_TOPOLOGY_COMPARE=1 \
ZLINK_CPP_E2E_EXPECT_TOPOLOGY_KEYS="$TOPOLOGY_KEYS" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR/dr-d4-client" \
  "$CLIENT" >"$LOG_DIR/dr-d4-client.stdout.log" 2>"$LOG_DIR/dr-d4-client.stderr.log"
grep -q "scenario DR-D4 passed" "$LOG_DIR/dr-d4-client.stdout.log"
echo "scenario DR-D4 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT <<<"$(ports 7)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"
R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"
R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"
R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"
R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_registry dr-a2-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB"
start_registry dr-a2-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER"
wait_member_peers "$R2_HTTP" 1
run_client dr-a2-client "$R2_ROUTER" "api-a"
echo "scenario DR-A2 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP R3_PUB R3_ROUTER R3_HTTP A_ENDPOINT B_ENDPOINT <<<"$(ports 11)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"; R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"; R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"; R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"; R2_HTTP="http://127.0.0.1:$R2_HTTP"
R3_PUB="tcp://127.0.0.1:$R3_PUB"; R3_ROUTER="tcp://127.0.0.1:$R3_ROUTER"; R3_HTTP="http://127.0.0.1:$R3_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"; B_ENDPOINT="tcp://127.0.0.1:$B_ENDPOINT"
start_registry dr-a3-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB,$R3_PUB"
start_registry dr-a3-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB,$R3_PUB"
start_registry dr-a3-reg3 "$R3_PUB" "$R3_ROUTER" "$R3_HTTP" "$R1_PUB,$R2_PUB"
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER"
start_provider api-b "$B_ENDPOINT" "$R3_ROUTER"
wait_member_peers "$R1_HTTP" 2
wait_member_peers "$R2_HTTP" 2
wait_member_peers "$R3_HTTP" 2
run_client dr-a3-client-r1 "$R1_ROUTER" "api-a,api-b"
run_client dr-a3-client-r2 "$R2_ROUTER" "api-a,api-b"
run_client dr-a3-client-r3 "$R3_ROUTER" "api-a,api-b"
echo "scenario DR-A3 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT B_ENDPOINT <<<"$(ports 8)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"; R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"; R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"; R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"; R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"; B_ENDPOINT="tcp://127.0.0.1:$B_ENDPOINT"
start_registry dr-a4-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB"
start_registry dr-a4-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
start_provider_instance api-a api-a-p1 "$A_ENDPOINT" "$R1_ROUTER"
start_provider_instance api-a api-a-p2 "$B_ENDPOINT" "$R2_ROUTER"
run_client dr-a4-client-r1 "$R1_ROUTER" "api-a"
run_client dr-a4-client-r2 "$R2_ROUTER" "api-a"
echo "scenario DR-A4 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT <<<"$(ports 7)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"; R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"; R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"; R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"; R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_registry dr-b1-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB"
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER"
wait_member_peers "$R1_HTTP" 1
run_client dr-b1-client-before "$R1_ROUTER" "api-a"
start_registry dr-b1-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
wait_member_peers "$R2_HTTP" 1
run_client dr-b1-client-after "$R2_ROUTER" "api-a"
echo "scenario DR-B1 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT <<<"$(ports 7)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"; R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"; R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"; R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"; R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_registry dr-b2-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB"
start_registry dr-b2-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
REG2_PID="$LAST_PID"
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER,$R2_ROUTER"
wait_member_peers "$R1_HTTP" 1
wait_member_peers "$R2_HTTP" 1
stop_pid "$REG2_PID"
run_client dr-b2-client "$R1_ROUTER" "api-a"
echo "scenario DR-B2 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT <<<"$(ports 7)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"; R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"; R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"; R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"; R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_registry dr-b3-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB"
start_registry dr-b3-reg2-a "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
REG2_PID="$LAST_PID"
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER,$R2_ROUTER"
wait_member_peers "$R1_HTTP" 1
wait_member_peers "$R2_HTTP" 1
for cycle in 1 2; do
  stop_pid "$REG2_PID"
  run_client "dr-b3-client-r1-down-$cycle" "$R1_ROUTER" "api-a"
  start_registry "dr-b3-reg2-b-$cycle" "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
  REG2_PID="$LAST_PID"
  wait_member_peers "$R2_HTTP" 1
  run_client "dr-b3-client-r2-up-$cycle" "$R2_ROUTER" "api-a"
done
echo "scenario DR-B3 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER R1_HTTP R2_PUB R2_ROUTER R2_HTTP A_ENDPOINT <<<"$(ports 7)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"; R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"; R1_HTTP="http://127.0.0.1:$R1_HTTP"
R2_PUB="tcp://127.0.0.1:$R2_PUB"; R2_ROUTER="tcp://127.0.0.1:$R2_ROUTER"; R2_HTTP="http://127.0.0.1:$R2_HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_registry dr-c1-reg1 "$R1_PUB" "$R1_ROUTER" "$R1_HTTP" "$R2_PUB"
start_registry dr-c1-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
REG2_PID="$LAST_PID"
start_provider api-a "$A_ENDPOINT" "$R1_ROUTER"
wait_member_peers "$R1_HTTP" 1
stop_pid "$REG2_PID"
run_client dr-c1-client "$R1_ROUTER" "api-a"
if python3 - "$R2_HTTP" <<'PY'
import sys, urllib.request
try:
    urllib.request.urlopen(sys.argv[1] + "/evidence", timeout=1)
    sys.exit(0)
except Exception:
    sys.exit(1)
PY
then
  echo "dead registry endpoint unexpectedly answered" >&2
  exit 1
fi
echo "scenario DR-C1 passed"
start_registry dr-c2-reg2 "$R2_PUB" "$R2_ROUTER" "$R2_HTTP" "$R1_PUB"
wait_member_peers "$R2_HTTP" 1
run_client dr-c2-client "$R2_ROUTER" "api-a"
echo "scenario DR-C2 passed"
echo "scenario DR-D2 passed"
kill "${PIDS[@]}" >/dev/null 2>&1 || true
wait >/dev/null 2>&1 || true
PIDS=()

read -r R1_PUB R1_ROUTER HTTP A_ENDPOINT <<<"$(ports 4)"
R1_PUB="tcp://127.0.0.1:$R1_PUB"
R1_ROUTER="tcp://127.0.0.1:$R1_ROUTER"
HTTP="http://127.0.0.1:$HTTP"
A_ENDPOINT="tcp://127.0.0.1:$A_ENDPOINT"
start_embedded_provider api-a "$A_ENDPOINT" "$R1_PUB" "$R1_ROUTER" "$HTTP"
run_client dr-d1-client "$R1_ROUTER" "api-a"
echo "scenario DR-D1 passed"
