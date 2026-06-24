#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

SERVER_PROJECT="$ROOT_DIR/Server/DiscoveryRegistryHa.Server.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/DiscoveryRegistryHa.Client.csproj"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

REG1_HTTP_PORT="$(pick_port)"
REG2_HTTP_PORT="$(pick_port)"
REG3_HTTP_PORT="$(pick_port)"
REG1_PUB_PORT="$(pick_port)"
REG2_PUB_PORT="$(pick_port)"
REG3_PUB_PORT="$(pick_port)"
REG1_ROUTER_PORT="$(pick_port)"
REG2_ROUTER_PORT="$(pick_port)"
REG3_ROUTER_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"

REG1_URL="http://127.0.0.1:$REG1_HTTP_PORT"
REG2_URL="http://127.0.0.1:$REG2_HTTP_PORT"
REG3_URL="http://127.0.0.1:$REG3_HTTP_PORT"
REG1_PUB="tcp://127.0.0.1:$REG1_PUB_PORT"
REG2_PUB="tcp://127.0.0.1:$REG2_PUB_PORT"
REG3_PUB="tcp://127.0.0.1:$REG3_PUB_PORT"
REG1_ROUTER="tcp://127.0.0.1:$REG1_ROUTER_PORT"
REG2_ROUTER="tcp://127.0.0.1:$REG2_ROUTER_PORT"
REG3_ROUTER="tcp://127.0.0.1:$REG3_ROUTER_PORT"
API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"

pids=()
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  wait "${pids[@]:-}" 2>/dev/null || true
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

wait_health() {
  local url="$1"
  local name="$2"
  for _ in $(seq 1 120); do
    if curl -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.25
  done
  echo "Timed out waiting for $name at $url" >&2
  return 1
}

start_registry() {
  local name="$1"
  local id="$2"
  local url="$3"
  local pub="$4"
  local router="$5"
  shift 5
  ZLINK_E2E_RID="$name" dotnet run --project "$SERVER_PROJECT" -- \
    --role registry \
    --rid "$name" \
    --registry-id "$id" \
    --http-url "$url" \
    --registry-pub-endpoint "$pub" \
    --registry-router-endpoint "$router" \
    --log-dir "$LOG_DIR" \
    "$@" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

start_provider() {
  local name="$1"
  local endpoint="$2"
  shift 2
  ZLINK_E2E_RID="$name" dotnet run --project "$SERVER_PROJECT" -- \
    --role provider \
    --rid "$name" \
    --http-url "http://127.0.0.1:$(pick_port)" \
    --channel-endpoint "$endpoint" \
    --evidence-file "$LOG_DIR/$name.evidence.log" \
    --log-dir "$LOG_DIR" \
    "$@" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
  wait_health "http://127.0.0.1:${!#}" "$name"
}

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

echo "log_dir=$LOG_DIR"

start_registry reg-1 1 "$REG1_URL" "$REG1_PUB" "$REG1_ROUTER" \
  --peer-pub-endpoint "$REG2_PUB" \
  --peer-pub-endpoint "$REG3_PUB"
wait_health "$REG1_URL" reg-1

ZLINK_E2E_RID="api-a" dotnet run --project "$SERVER_PROJECT" -- \
  --role provider \
  --rid api-a \
  --http-url "http://127.0.0.1:$(pick_port)" \
  --channel-endpoint "$API_A" \
  --discovery-endpoint "$REG1_ROUTER" \
  --evidence-file "$LOG_DIR/api-a.phase1.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-a.phase1.stdout.log" 2>"$LOG_DIR/api-a.phase1.stderr.log" &
api_a_phase1_pid="$!"
pids+=("$api_a_phase1_pid")

ZLINK_E2E_RID="api-b" dotnet run --project "$SERVER_PROJECT" -- \
  --role provider \
  --rid api-b \
  --http-url "http://127.0.0.1:$(pick_port)" \
  --channel-endpoint "$API_B" \
  --discovery-endpoint "$REG1_ROUTER" \
  --evidence-file "$LOG_DIR/api-b.phase1.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-b.phase1.stdout.log" 2>"$LOG_DIR/api-b.phase1.stderr.log" &
api_b_phase1_pid="$!"
pids+=("$api_b_phase1_pid")

dotnet run --project "$CLIENT_PROJECT" -- \
  --scenario a1 \
  --reg-1-url "$REG1_URL" \
  --reg-1-router-endpoint "$REG1_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client-a1.stdout.log" 2>"$LOG_DIR/client-a1.stderr.log"
cat "$LOG_DIR/client-a1.stdout.log"

stop_pid "$api_a_phase1_pid"
stop_pid "$api_b_phase1_pid"
sleep 0.5

start_registry reg-2 2 "$REG2_URL" "$REG2_PUB" "$REG2_ROUTER" \
  --peer-pub-endpoint "$REG1_PUB" \
  --peer-pub-endpoint "$REG3_PUB"
wait_health "$REG2_URL" reg-2

start_registry reg-3 3 "$REG3_URL" "$REG3_PUB" "$REG3_ROUTER" \
  --peer-pub-endpoint "$REG1_PUB" \
  --peer-pub-endpoint "$REG2_PUB"
wait_health "$REG3_URL" reg-3

ZLINK_E2E_RID="api-a" dotnet run --project "$SERVER_PROJECT" -- \
  --role provider \
  --rid api-a \
  --http-url "http://127.0.0.1:$(pick_port)" \
  --channel-endpoint "$API_A" \
  --discovery-endpoint "$REG1_ROUTER" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-a.stdout.log" 2>"$LOG_DIR/api-a.stderr.log" &
pids+=("$!")

ZLINK_E2E_RID="api-b" dotnet run --project "$SERVER_PROJECT" -- \
  --role provider \
  --rid api-b \
  --http-url "http://127.0.0.1:$(pick_port)" \
  --channel-endpoint "$API_B" \
  --discovery-endpoint "$REG3_ROUTER" \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-b.stdout.log" 2>"$LOG_DIR/api-b.stderr.log" &
pids+=("$!")

dotnet run --project "$CLIENT_PROJECT" -- \
  --scenario cluster \
  --reg-1-url "$REG1_URL" \
  --reg-2-url "$REG2_URL" \
  --reg-3-url "$REG3_URL" \
  --reg-1-router-endpoint "$REG1_ROUTER" \
  --reg-2-router-endpoint "$REG2_ROUTER" \
  --reg-3-router-endpoint "$REG3_ROUTER" \
  --reg-2-pub-endpoint "$REG2_PUB" \
  --reg-2-peer-pub-endpoint "$REG1_PUB" \
  --reg-2-peer-pub-endpoint "$REG3_PUB" \
  --api-a-endpoint "$API_A" \
  --api-b-endpoint "$API_B" \
  --server-project "$SERVER_PROJECT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client-cluster.stdout.log" 2>"$LOG_DIR/client-cluster.stderr.log"
cat "$LOG_DIR/client-cluster.stdout.log"
