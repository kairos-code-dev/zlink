#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO_SET="${1:-${SCENARIO_SET:-all}}"
mkdir -p "$LOG_DIR"

REGISTRY_PROJECT="$ROOT_DIR/Server/Registry/DiscoveryRegistryHa.Registry.csproj"
PROVIDER_PROJECT="$ROOT_DIR/Server/Provider/DiscoveryRegistryHa.Provider.csproj"
EMBEDDED_PROJECT="$ROOT_DIR/Server/Embedded/DiscoveryRegistryHa.Embedded.csproj"
CONSUMER_PROJECT="$ROOT_DIR/Server/Consumer/DiscoveryRegistryHa.Consumer.csproj"
PROBE_PROJECT="$ROOT_DIR/Server/Probe/DiscoveryRegistryHa.Probe.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/DiscoveryRegistryHa.Client.csproj"

if [[ "$SCENARIO_SET" == "all" ]]; then
  for scenario in \
    a1 \
    dr-a2 \
    dr-a3 \
    dr-a4 \
    dr-b1 \
    dr-b2 \
    dr-b3 \
    dr-c1 \
    dr-c2 \
    dr-c3 \
    dr-d1 \
    dr-d2 \
    dr-d3 \
    dr-d4; do
    SCENARIO_SET="$scenario" "$0"
  done
  echo "discovery-registry-ha e2e result=passed"
  exit 0
fi

read -r -a PORTS <<<"$(python3 - <<'PY'
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 30:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        if port in chosen:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"

REG1_HTTP_PORT="${PORTS[0]}"
REG2_HTTP_PORT="${PORTS[1]}"
REG3_HTTP_PORT="${PORTS[2]}"
REG1_PUB_PORT="${PORTS[3]}"
REG2_PUB_PORT="${PORTS[4]}"
REG3_PUB_PORT="${PORTS[5]}"
REG1_ROUTER_PORT="${PORTS[6]}"
REG2_ROUTER_PORT="${PORTS[7]}"
REG3_ROUTER_PORT="${PORTS[8]}"
API_A_PORT="${PORTS[9]}"
API_B_PORT="${PORTS[10]}"
API_A_PHASE1_HTTP_PORT="${PORTS[11]}"
API_B_PHASE1_HTTP_PORT="${PORTS[12]}"
API_A_HTTP_PORT="${PORTS[13]}"
API_B_HTTP_PORT="${PORTS[14]}"
CONSUMER_HTTP_PORT="${PORTS[15]}"
CLUSTER_CONSUMER_HTTP_PORT="${PORTS[16]}"
REG3_CONSUMER_HTTP_PORT="${PORTS[17]}"
DUPLICATE_PROVIDER_HTTP_PORT="${PORTS[18]}"
DUPLICATE_PROVIDER_PORT="${PORTS[19]}"
REG1_REG2_CONSUMER_HTTP_PORT="${PORTS[20]}"
PROBE_HTTP_PORT="${PORTS[21]}"
EMBEDDED_HTTP_PORT="${PORTS[22]}"
EMBEDDED_PUB_PORT="${PORTS[23]}"
EMBEDDED_ROUTER_PORT="${PORTS[24]}"
EMBEDDED_CHANNEL_PORT="${PORTS[25]}"
EMBEDDED_CONSUMER_HTTP_PORT="${PORTS[26]}"
API_C_HTTP_PORT="${PORTS[27]}"
API_C_PORT="${PORTS[28]}"

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
API_A_DUPLICATE="tcp://127.0.0.1:$DUPLICATE_PROVIDER_PORT"
EMBEDDED_PUB="tcp://127.0.0.1:$EMBEDDED_PUB_PORT"
EMBEDDED_ROUTER="tcp://127.0.0.1:$EMBEDDED_ROUTER_PORT"
EMBEDDED_CHANNEL="tcp://127.0.0.1:$EMBEDDED_CHANNEL_PORT"
API_C="tcp://127.0.0.1:$API_C_PORT"

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
  ZLINK_E2E_RID="$name" dotnet run --no-build --project "$REGISTRY_PROJECT" -- \
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

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

echo "log_dir=$LOG_DIR"
dotnet build "$REGISTRY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$PROVIDER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$EMBEDDED_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CONSUMER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$PROBE_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

start_registry reg-1 1 "$REG1_URL" "$REG1_PUB" "$REG1_ROUTER" \
  --peer-pub-endpoint "$REG2_PUB" \
  --peer-pub-endpoint "$REG3_PUB"
wait_health "$REG1_URL" reg-1

ZLINK_E2E_RID="api-a" dotnet run --no-build --project "$PROVIDER_PROJECT" -- \
  --rid api-a \
  --http-url "http://127.0.0.1:$API_A_PHASE1_HTTP_PORT" \
  --channel-endpoint "$API_A" \
  --discovery-endpoint "$REG1_ROUTER" \
  --evidence-file "$LOG_DIR/api-a.phase1.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-a.phase1.stdout.log" 2>"$LOG_DIR/api-a.phase1.stderr.log" &
api_a_phase1_pid="$!"
pids+=("$api_a_phase1_pid")

ZLINK_E2E_RID="api-b" dotnet run --no-build --project "$PROVIDER_PROJECT" -- \
  --rid api-b \
  --http-url "http://127.0.0.1:$API_B_PHASE1_HTTP_PORT" \
  --channel-endpoint "$API_B" \
  --discovery-endpoint "$REG1_ROUTER" \
  --evidence-file "$LOG_DIR/api-b.phase1.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-b.phase1.stdout.log" 2>"$LOG_DIR/api-b.phase1.stderr.log" &
api_b_phase1_pid="$!"
pids+=("$api_b_phase1_pid")

ZLINK_E2E_RID="probe-reg1" dotnet run --no-build --project "$PROBE_PROJECT" -- \
  --http-url "http://127.0.0.1:$PROBE_HTTP_PORT" \
  --registry-router-endpoint "$REG1_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/probe-reg1.stdout.log" 2>"$LOG_DIR/probe-reg1.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$PROBE_HTTP_PORT" probe-reg1

ZLINK_E2E_RID="consumer-a1" dotnet run --no-build --project "$CONSUMER_PROJECT" -- \
  --rid consumer-a1 \
  --http-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --discovery-endpoint "$REG1_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/consumer-a1.stdout.log" 2>"$LOG_DIR/consumer-a1.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$CONSUMER_HTTP_PORT" consumer-a1

dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --scenario a1 \
  --probe-url "http://127.0.0.1:$PROBE_HTTP_PORT" \
  --consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --provider-a-url "http://127.0.0.1:$API_A_PHASE1_HTTP_PORT" \
  --provider-b-url "http://127.0.0.1:$API_B_PHASE1_HTTP_PORT" \
  --reg-1-url "$REG1_URL" \
  --reg-2-url "$REG2_URL" \
  --reg-3-url "$REG3_URL" \
  --reg-1-router-endpoint "$REG1_ROUTER" \
  --reg-2-router-endpoint "$REG2_ROUTER" \
  --reg-3-router-endpoint "$REG3_ROUTER" \
  --reg-1-pub-endpoint "$REG1_PUB" \
  --reg-2-pub-endpoint "$REG2_PUB" \
  --reg-3-pub-endpoint "$REG3_PUB" \
  --reg-2-peer-pub-endpoint "$REG1_PUB" \
  --reg-2-peer-pub-endpoint "$REG3_PUB" \
  --api-a-endpoint "$API_A" \
  --api-b-endpoint "$API_B" \
  --registry-project "$REGISTRY_PROJECT" \
  --provider-project "$PROVIDER_PROJECT" \
  --consumer-project "$CONSUMER_PROJECT" \
  --embedded-project "$EMBEDDED_PROJECT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client-a1.stdout.log" 2>"$LOG_DIR/client-a1.stderr.log"
cat "$LOG_DIR/client-a1.stdout.log"
if [[ "$SCENARIO_SET" == "a1" ]]; then
  echo "discovery-registry-ha e2e scenario=a1 result=passed"
  exit 0
fi

stop_pid "$api_a_phase1_pid"
stop_pid "$api_b_phase1_pid"
sleep 0.5

start_registry reg-2 2 "$REG2_URL" "$REG2_PUB" "$REG2_ROUTER" \
  --peer-pub-endpoint "$REG1_PUB" \
  --peer-pub-endpoint "$REG3_PUB"
reg2_pid="${pids[$((${#pids[@]} - 1))]}"
wait_health "$REG2_URL" reg-2

start_registry reg-3 3 "$REG3_URL" "$REG3_PUB" "$REG3_ROUTER" \
  --peer-pub-endpoint "$REG1_PUB" \
  --peer-pub-endpoint "$REG2_PUB"
wait_health "$REG3_URL" reg-3

ZLINK_E2E_RID="api-a" dotnet run --no-build --project "$PROVIDER_PROJECT" -- \
  --rid api-a \
  --http-url "http://127.0.0.1:$API_A_HTTP_PORT" \
  --channel-endpoint "$API_A" \
  --discovery-endpoint "$REG1_ROUTER" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/api-a.stdout.log" 2>"$LOG_DIR/api-a.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$API_A_HTTP_PORT" api-a

if [[ "$SCENARIO_SET" != "dr-a2" && "$SCENARIO_SET" != "dr-a4" ]]; then
  ZLINK_E2E_RID="api-b" dotnet run --no-build --project "$PROVIDER_PROJECT" -- \
    --rid api-b \
    --http-url "http://127.0.0.1:$API_B_HTTP_PORT" \
    --channel-endpoint "$API_B" \
    --discovery-endpoint "$REG3_ROUTER" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR" \
    >"$LOG_DIR/api-b.stdout.log" 2>"$LOG_DIR/api-b.stderr.log" &
  pids+=("$!")
  wait_health "http://127.0.0.1:$API_B_HTTP_PORT" api-b
fi

ZLINK_E2E_RID="consumer-reg2" dotnet run --no-build --project "$CONSUMER_PROJECT" -- \
  --rid consumer-reg2 \
  --http-url "http://127.0.0.1:$CLUSTER_CONSUMER_HTTP_PORT" \
  --discovery-endpoint "$REG2_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/consumer-reg2.stdout.log" 2>"$LOG_DIR/consumer-reg2.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$CLUSTER_CONSUMER_HTTP_PORT" consumer-reg2

ZLINK_E2E_RID="consumer-reg3" dotnet run --no-build --project "$CONSUMER_PROJECT" -- \
  --rid consumer-reg3 \
  --http-url "http://127.0.0.1:$REG3_CONSUMER_HTTP_PORT" \
  --discovery-endpoint "$REG3_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/consumer-reg3.stdout.log" 2>"$LOG_DIR/consumer-reg3.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$REG3_CONSUMER_HTTP_PORT" consumer-reg3

ZLINK_E2E_RID="consumer-reg1-reg2" dotnet run --no-build --project "$CONSUMER_PROJECT" -- \
  --rid consumer-reg1-reg2 \
  --http-url "http://127.0.0.1:$REG1_REG2_CONSUMER_HTTP_PORT" \
  --discovery-endpoint "$REG1_ROUTER" \
  --discovery-endpoint "$REG2_ROUTER" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/consumer-reg1-reg2.stdout.log" 2>"$LOG_DIR/consumer-reg1-reg2.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$REG1_REG2_CONSUMER_HTTP_PORT" consumer-reg1-reg2

if [[ "$SCENARIO_SET" == "dr-a4" ]]; then
  ZLINK_E2E_RID="api-a" dotnet run --no-build --project "$PROVIDER_PROJECT" -- \
    --rid api-a \
    --http-url "http://127.0.0.1:$DUPLICATE_PROVIDER_HTTP_PORT" \
    --channel-endpoint "$API_A_DUPLICATE" \
    --discovery-endpoint "$REG2_ROUTER" \
    --evidence-file "$LOG_DIR/api-a-duplicate.evidence.log" \
    --log-dir "$LOG_DIR" \
    >"$LOG_DIR/api-a-duplicate.stdout.log" 2>"$LOG_DIR/api-a-duplicate.stderr.log" &
  pids+=("$!")
  wait_health "http://127.0.0.1:$DUPLICATE_PROVIDER_HTTP_PORT" api-a-duplicate
fi

if [[ "$SCENARIO_SET" == "dr-d1" || "$SCENARIO_SET" == "dr-d3" ]]; then
  embedded_rid="embedded-api"
  embedded_name="embedded-dr-d1"
  embedded_peers=()
  if [[ "$SCENARIO_SET" == "dr-d3" ]]; then
    embedded_rid="embedded-api-mixed"
    embedded_name="embedded-dr-d3"
    embedded_peers=(--peer-pub-endpoint "$REG1_PUB")
  fi

  ZLINK_E2E_RID="$embedded_rid" dotnet run --no-build --project "$EMBEDDED_PROJECT" -- \
    --rid "$embedded_rid" \
    --registry-id 10 \
    --http-url "http://127.0.0.1:$EMBEDDED_HTTP_PORT" \
    --registry-pub-endpoint "$EMBEDDED_PUB" \
    --registry-router-endpoint "$EMBEDDED_ROUTER" \
    --channel-endpoint "$EMBEDDED_CHANNEL" \
    --evidence-file "$LOG_DIR/$embedded_name.evidence.log" \
    --log-dir "$LOG_DIR" \
    "${embedded_peers[@]}" \
    >"$LOG_DIR/$embedded_name.stdout.log" 2>"$LOG_DIR/$embedded_name.stderr.log" &
  pids+=("$!")
  wait_health "http://127.0.0.1:$EMBEDDED_HTTP_PORT" "$embedded_name"

  ZLINK_E2E_RID="consumer-$embedded_name" dotnet run --no-build --project "$CONSUMER_PROJECT" -- \
    --rid "consumer-$embedded_name" \
    --http-url "http://127.0.0.1:$EMBEDDED_CONSUMER_HTTP_PORT" \
    --discovery-endpoint "$EMBEDDED_ROUTER" \
    --log-dir "$LOG_DIR" \
    >"$LOG_DIR/consumer-$embedded_name.stdout.log" 2>"$LOG_DIR/consumer-$embedded_name.stderr.log" &
  pids+=("$!")
  wait_health "http://127.0.0.1:$EMBEDDED_CONSUMER_HTTP_PORT" "consumer-$embedded_name"
fi

if [[ "$SCENARIO_SET" == "dr-b2" || "$SCENARIO_SET" == "dr-c1" ]]; then
  stop_pid "$reg2_pid"
fi

if [[ "$SCENARIO_SET" == "dr-b3" ]]; then
  for index in 0 1; do
    stop_pid "$reg2_pid"
    start_registry "reg-2-flap-$index" 2 "$REG2_URL" "$REG2_PUB" "$REG2_ROUTER" \
      --peer-pub-endpoint "$REG1_PUB" \
      --peer-pub-endpoint "$REG3_PUB"
    reg2_pid="${pids[$((${#pids[@]} - 1))]}"
    wait_health "$REG2_URL" "reg-2-flap-$index"
  done
fi

if [[ "$SCENARIO_SET" == "dr-c2" ]]; then
  stop_pid "$reg2_pid"
  start_registry "reg-2-recovered" 2 "$REG2_URL" "$REG2_PUB" "$REG2_ROUTER" \
    --peer-pub-endpoint "$REG1_PUB" \
    --peer-pub-endpoint "$REG3_PUB"
  reg2_pid="${pids[$((${#pids[@]} - 1))]}"
  wait_health "$REG2_URL" "reg-2-recovered"
fi

if [[ "$SCENARIO_SET" == "dr-a2" || "$SCENARIO_SET" == "dr-a3" || "$SCENARIO_SET" == "dr-a4" || "$SCENARIO_SET" == "dr-b1" || "$SCENARIO_SET" == "dr-b2" || "$SCENARIO_SET" == "dr-d2" || "$SCENARIO_SET" == "dr-d4" || "$SCENARIO_SET" == "dr-b3" || "$SCENARIO_SET" == "dr-d1" || "$SCENARIO_SET" == "dr-d3" || "$SCENARIO_SET" == "dr-c1" || "$SCENARIO_SET" == "dr-c2" || "$SCENARIO_SET" == "dr-c3" ]]; then
dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --scenario "$SCENARIO_SET" \
  --probe-url "http://127.0.0.1:$PROBE_HTTP_PORT" \
  --consumer-url "http://127.0.0.1:$CLUSTER_CONSUMER_HTTP_PORT" \
  --reg-1-consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --reg-2-consumer-url "http://127.0.0.1:$CLUSTER_CONSUMER_HTTP_PORT" \
  --reg-3-consumer-url "http://127.0.0.1:$REG3_CONSUMER_HTTP_PORT" \
  --reg-1-reg-2-consumer-url "http://127.0.0.1:$REG1_REG2_CONSUMER_HTTP_PORT" \
  --provider-a-url "http://127.0.0.1:$API_A_HTTP_PORT" \
  --provider-b-url "http://127.0.0.1:$API_B_HTTP_PORT" \
  --duplicate-provider-url "http://127.0.0.1:$DUPLICATE_PROVIDER_HTTP_PORT" \
  --duplicate-provider-endpoint "$API_A_DUPLICATE" \
  --embedded-url "http://127.0.0.1:$EMBEDDED_HTTP_PORT" \
  --embedded-consumer-url "http://127.0.0.1:$EMBEDDED_CONSUMER_HTTP_PORT" \
  --provider-c-url "http://127.0.0.1:$API_C_HTTP_PORT" \
  --provider-c-endpoint "$API_C" \
  --reg-1-url "$REG1_URL" \
  --reg-2-url "$REG2_URL" \
  --reg-3-url "$REG3_URL" \
  --reg-1-router-endpoint "$REG1_ROUTER" \
  --reg-2-router-endpoint "$REG2_ROUTER" \
  --reg-3-router-endpoint "$REG3_ROUTER" \
  --reg-1-pub-endpoint "$REG1_PUB" \
  --reg-2-pub-endpoint "$REG2_PUB" \
  --reg-3-pub-endpoint "$REG3_PUB" \
  --reg-2-peer-pub-endpoint "$REG1_PUB" \
  --reg-2-peer-pub-endpoint "$REG3_PUB" \
  --api-a-endpoint "$API_A" \
  --api-b-endpoint "$API_B" \
  --registry-project "$REGISTRY_PROJECT" \
  --provider-project "$PROVIDER_PROJECT" \
  --consumer-project "$CONSUMER_PROJECT" \
  --embedded-project "$EMBEDDED_PROJECT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client-${SCENARIO_SET}.stdout.log" 2>"$LOG_DIR/client-${SCENARIO_SET}.stderr.log"
cat "$LOG_DIR/client-${SCENARIO_SET}.stdout.log"
echo "discovery-registry-ha e2e scenario=${SCENARIO_SET} result=passed"
exit 0
fi

echo "Unsupported SCENARIO_SET=$SCENARIO_SET" >&2
exit 1
