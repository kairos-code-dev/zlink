#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-DR-A1}"
mkdir -p "$LOG_DIR"

pick_port() {
  node -e "const net=require('node:net'); const s=net.createServer(); s.listen(0,'127.0.0.1',()=>{console.log(s.address().port); s.close();});"
}

build_package() {
  local dir="$1"
  (cd "$dir" && npm run build >/dev/null)
}

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
    echo "E2E failed. log_dir=$LOG_DIR" >&2
    for file in "$LOG_DIR"/*.stderr.log "$LOG_DIR"/client.stderr.log; do
      if [[ -f "$file" ]]; then
        echo "----- $file -----" >&2
        tail -n 80 "$file" >&2 || true
      fi
    done
  fi
}
trap cleanup EXIT

start_server() {
  local name="$1"
  local main="$2"
  shift
  shift
  ZLINK_E2E_RID="$name" node "$main" "$@" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

stop_pid() {
  local pid="$1"
  if ! kill -0 "$pid" 2>/dev/null; then
    return 0
  fi
  kill "$pid" 2>/dev/null || true
  for _ in $(seq 1 50); do
    if ! kill -0 "$pid" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  kill -9 "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

kill_pid() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/Registry"
build_package "$ROOT_DIR/Server/Provider"
build_package "$ROOT_DIR/Server/Consumer"
build_package "$ROOT_DIR/Server/Embedded"
build_package "$ROOT_DIR/Server/Probe"
build_package "$ROOT_DIR/Client"

REGISTRY_MAIN="$ROOT_DIR/Server/Registry/dist/Server/Registry/main.js"
PROVIDER_MAIN="$ROOT_DIR/Server/Provider/dist/Server/Provider/main.js"
CONSUMER_MAIN="$ROOT_DIR/Server/Consumer/dist/Server/Consumer/main.js"
EMBEDDED_MAIN="$ROOT_DIR/Server/Embedded/dist/Server/Embedded/main.js"
PROBE_MAIN="$ROOT_DIR/Server/Probe/dist/Server/Probe/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

run_dr_a1() {
  local reg_http_port consumer_http_port provider_a_http_port provider_b_http_port
  local reg_pub_port reg_router_port provider_a_channel_port provider_b_channel_port
  reg_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg_pub_port="$(pick_port)"
  reg_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg_url="http://127.0.0.1:$reg_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg_pub="tcp://127.0.0.1:$reg_pub_port"
  local reg_router="tcp://127.0.0.1:$reg_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg_url" \
    --registry-pub-endpoint "$reg_pub" \
    --registry-router-endpoint "$reg_router" \
    --log-dir "$LOG_DIR"
  wait_health "$reg_url" reg-1

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg_router" \
    --trace-label consumer \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer

  node "$CLIENT_MAIN" \
    --registry-url "$reg_url" \
    --consumer-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-A1 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_a2() {
  local reg1_http_port reg2_http_port consumer_http_port provider_a_http_port
  local reg1_pub_port reg2_pub_port reg1_router_port reg2_router_port provider_a_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg2_url" reg-2

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server consumer "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-dr-a2 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --consumer-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --scenario DR-A2 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_a3() {
  local reg1_http_port reg2_http_port reg3_http_port
  local consumer1_http_port consumer2_http_port consumer3_http_port
  local provider_a_http_port provider_b_http_port
  local reg1_pub_port reg2_pub_port reg3_pub_port
  local reg1_router_port reg2_router_port reg3_router_port
  local provider_a_channel_port provider_b_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  reg3_http_port="$(pick_port)"
  consumer1_http_port="$(pick_port)"
  consumer2_http_port="$(pick_port)"
  consumer3_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg3_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  reg3_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local reg3_url="http://127.0.0.1:$reg3_http_port"
  local consumer1_url="http://127.0.0.1:$consumer1_http_port"
  local consumer2_url="http://127.0.0.1:$consumer2_http_port"
  local consumer3_url="http://127.0.0.1:$consumer3_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg3_pub="tcp://127.0.0.1:$reg3_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"
  local reg3_router="tcp://127.0.0.1:$reg3_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg2_url" reg-2

  start_server reg-3 "$REGISTRY_MAIN" \
    --rid reg-3 \
    --registry-id 3 \
    --http-url "$reg3_url" \
    --registry-pub-endpoint "$reg3_pub" \
    --registry-router-endpoint "$reg3_router" \
    --peer "$reg1_pub" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg3_url" reg-3

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg3_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer-reg-1 "$CONSUMER_MAIN" \
    --http-url "$consumer1_url" \
    --registry-router-endpoint "$reg1_router" \
    --trace-label consumer-reg-1 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer1_url" consumer-reg-1

  start_server consumer-reg-2 "$CONSUMER_MAIN" \
    --http-url "$consumer2_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-2 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer2_url" consumer-reg-2

  start_server consumer-reg-3 "$CONSUMER_MAIN" \
    --http-url "$consumer3_url" \
    --registry-router-endpoint "$reg3_router" \
    --trace-label consumer-reg-3 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer3_url" consumer-reg-3

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --registry-3-url "$reg3_url" \
    --consumer-url "$consumer1_url" \
    --consumer-2-url "$consumer2_url" \
    --consumer-3-url "$consumer3_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-A3 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_a4() {
  local reg1_http_port reg2_http_port consumer_http_port provider_a_http_port duplicate_provider_http_port
  local reg1_pub_port reg2_pub_port reg1_router_port reg2_router_port provider_a_channel_port duplicate_provider_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  duplicate_provider_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  duplicate_provider_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local duplicate_provider_url="http://127.0.0.1:$duplicate_provider_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg2_url" reg-2

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-a-duplicate "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$duplicate_provider_url" \
    --registry-router-endpoint "$reg2_router" \
    --channel-endpoint "tcp://127.0.0.1:$duplicate_provider_channel_port" \
    --evidence-file "$LOG_DIR/api-a-duplicate.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$duplicate_provider_url" api-a-duplicate

  start_server consumer-reg-2 "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-2-dr-a4 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer-reg-2

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --consumer-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --duplicate-provider-url "$duplicate_provider_url" \
    --scenario DR-A4 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_b1() {
  local reg1_http_port reg2_http_port reg3_http_port
  local consumer2_http_port consumer3_http_port
  local provider_a_http_port provider_b_http_port
  local reg1_pub_port reg2_pub_port reg3_pub_port
  local reg1_router_port reg2_router_port reg3_router_port
  local provider_a_channel_port provider_b_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  reg3_http_port="$(pick_port)"
  consumer2_http_port="$(pick_port)"
  consumer3_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg3_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  reg3_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local reg3_url="http://127.0.0.1:$reg3_http_port"
  local consumer2_url="http://127.0.0.1:$consumer2_http_port"
  local consumer3_url="http://127.0.0.1:$consumer3_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg3_pub="tcp://127.0.0.1:$reg3_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"
  local reg3_router="tcp://127.0.0.1:$reg3_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg2_url" reg-2

  start_server reg-3 "$REGISTRY_MAIN" \
    --rid reg-3 \
    --registry-id 3 \
    --http-url "$reg3_url" \
    --registry-pub-endpoint "$reg3_pub" \
    --registry-router-endpoint "$reg3_router" \
    --peer "$reg1_pub" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg3_url" reg-3

  start_server consumer-reg-2 "$CONSUMER_MAIN" \
    --http-url "$consumer2_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-2-dr-b1 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer2_url" consumer-reg-2

  start_server consumer-reg-3 "$CONSUMER_MAIN" \
    --http-url "$consumer3_url" \
    --registry-router-endpoint "$reg3_router" \
    --trace-label consumer-reg-3-dr-b1 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer3_url" consumer-reg-3

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --registry-3-url "$reg3_url" \
    --consumer-url "$consumer2_url" \
    --consumer-2-url "$consumer2_url" \
    --consumer-3-url "$consumer3_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-B1 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_b2() {
  local reg1_http_port reg2_http_port consumer_http_port provider_a_http_port provider_b_http_port
  local reg1_pub_port reg2_pub_port reg1_router_port reg2_router_port
  local provider_a_channel_port provider_b_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  local reg2_pid="${pids[$((${#pids[@]} - 1))]}"
  wait_health "$reg2_url" reg-2

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --registry-router-endpoint "$reg2_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg1_router" \
    --registry-router-endpoint "$reg2_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer-reg-1-reg-2 "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg1_router" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-1-reg-2-dr-b2 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer-reg-1-reg-2
  sleep 2.5

  stop_pid "$reg2_pid"

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --consumer-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-B2 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_b3() {
  local reg1_http_port reg2_http_port reg3_http_port consumer1_http_port consumer2_http_port
  local provider_a_http_port provider_b_http_port
  local reg1_pub_port reg2_pub_port reg3_pub_port
  local reg1_router_port reg2_router_port reg3_router_port
  local provider_a_channel_port provider_b_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  reg3_http_port="$(pick_port)"
  consumer1_http_port="$(pick_port)"
  consumer2_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg3_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  reg3_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local reg3_url="http://127.0.0.1:$reg3_http_port"
  local consumer1_url="http://127.0.0.1:$consumer1_http_port"
  local consumer2_url="http://127.0.0.1:$consumer2_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg3_pub="tcp://127.0.0.1:$reg3_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"
  local reg3_router="tcp://127.0.0.1:$reg3_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  local reg2_pid="${pids[$((${#pids[@]} - 1))]}"
  wait_health "$reg2_url" reg-2

  start_server reg-3 "$REGISTRY_MAIN" \
    --rid reg-3 \
    --registry-id 3 \
    --http-url "$reg3_url" \
    --registry-pub-endpoint "$reg3_pub" \
    --registry-router-endpoint "$reg3_router" \
    --peer "$reg1_pub" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg3_url" reg-3

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg3_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  for index in 0 1; do
    stop_pid "$reg2_pid"
    start_server "reg-2-flap-$index" "$REGISTRY_MAIN" \
      --rid reg-2 \
      --registry-id 2 \
      --http-url "$reg2_url" \
      --registry-pub-endpoint "$reg2_pub" \
      --registry-router-endpoint "$reg2_router" \
      --peer "$reg1_pub" \
      --peer "$reg3_pub" \
      --log-dir "$LOG_DIR"
    reg2_pid="${pids[$((${#pids[@]} - 1))]}"
    wait_health "$reg2_url" "reg-2-flap-$index"
  done

  start_server consumer-reg-1-dr-b3 "$CONSUMER_MAIN" \
    --http-url "$consumer1_url" \
    --registry-router-endpoint "$reg1_router" \
    --trace-label consumer-reg-1-dr-b3 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer1_url" consumer-reg-1-dr-b3

  start_server consumer-reg-2-dr-b3 "$CONSUMER_MAIN" \
    --http-url "$consumer2_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-2-dr-b3 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer2_url" consumer-reg-2-dr-b3

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --registry-3-url "$reg3_url" \
    --consumer-url "$consumer1_url" \
    --consumer-2-url "$consumer2_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-B3 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_c1() {
  local reg1_http_port reg2_http_port consumer_http_port provider_a_http_port provider_b_http_port
  local reg1_pub_port reg2_pub_port reg1_router_port reg2_router_port
  local provider_a_channel_port provider_b_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  local reg2_pid="${pids[$((${#pids[@]} - 1))]}"
  wait_health "$reg2_url" reg-2

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer-reg-1-dr-c1 "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg1_router" \
    --trace-label consumer-reg-1-dr-c1 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer-reg-1-dr-c1

  kill_pid "$reg2_pid"

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --consumer-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-C1 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_c2() {
  local reg1_http_port reg2_http_port consumer_http_port provider_a_http_port provider_b_http_port
  local reg1_pub_port reg2_pub_port reg1_router_port reg2_router_port
  local provider_a_channel_port provider_b_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  local reg2_pid="${pids[$((${#pids[@]} - 1))]}"
  wait_health "$reg2_url" reg-2

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  kill_pid "$reg2_pid"
  start_server reg-2-recovered "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg2_url" reg-2-recovered

  start_server consumer-reg-2-dr-c2 "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-2-dr-c2 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer-reg-2-dr-c2

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --consumer-url "$consumer_url" \
    --consumer-2-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-C2 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_c3() {
  local reg1_http_port reg2_http_port reg3_http_port consumer1_http_port consumer2_http_port
  local provider_a_http_port provider_b_http_port provider_c_http_port
  local reg1_pub_port reg2_pub_port reg3_pub_port
  local reg1_router_port reg2_router_port reg3_router_port
  local provider_a_channel_port provider_b_channel_port provider_c_channel_port
  reg1_http_port="$(pick_port)"
  reg2_http_port="$(pick_port)"
  reg3_http_port="$(pick_port)"
  consumer1_http_port="$(pick_port)"
  consumer2_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  provider_c_http_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg2_pub_port="$(pick_port)"
  reg3_pub_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg2_router_port="$(pick_port)"
  reg3_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"
  provider_c_channel_port="$(pick_port)"

  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg2_url="http://127.0.0.1:$reg2_http_port"
  local reg3_url="http://127.0.0.1:$reg3_http_port"
  local consumer1_url="http://127.0.0.1:$consumer1_http_port"
  local consumer2_url="http://127.0.0.1:$consumer2_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local provider_c_url="http://127.0.0.1:$provider_c_http_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg2_pub="tcp://127.0.0.1:$reg2_pub_port"
  local reg3_pub="tcp://127.0.0.1:$reg3_pub_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg2_router="tcp://127.0.0.1:$reg2_router_port"
  local reg3_router="tcp://127.0.0.1:$reg3_router_port"
  local provider_c_endpoint="tcp://127.0.0.1:$provider_c_channel_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$reg2_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-2 "$REGISTRY_MAIN" \
    --rid reg-2 \
    --registry-id 2 \
    --http-url "$reg2_url" \
    --registry-pub-endpoint "$reg2_pub" \
    --registry-router-endpoint "$reg2_router" \
    --peer "$reg1_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg2_url" reg-2

  start_server reg-3 "$REGISTRY_MAIN" \
    --rid reg-3 \
    --registry-id 3 \
    --http-url "$reg3_url" \
    --registry-pub-endpoint "$reg3_pub" \
    --registry-router-endpoint "$reg3_router" \
    --peer "$reg1_pub" \
    --peer "$reg2_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg3_url" reg-3

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg3_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer-reg-1-dr-c3 "$CONSUMER_MAIN" \
    --http-url "$consumer1_url" \
    --registry-router-endpoint "$reg1_router" \
    --trace-label consumer-reg-1-dr-c3 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer1_url" consumer-reg-1-dr-c3

  start_server consumer-reg-2-dr-c3 "$CONSUMER_MAIN" \
    --http-url "$consumer2_url" \
    --registry-router-endpoint "$reg2_router" \
    --trace-label consumer-reg-2-dr-c3 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer2_url" consumer-reg-2-dr-c3

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --registry-2-url "$reg2_url" \
    --registry-3-url "$reg3_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-2-pub-endpoint "$reg2_pub" \
    --registry-3-pub-endpoint "$reg3_pub" \
    --registry-router-endpoint "$reg1_router" \
    --registry-2-router-endpoint "$reg2_router" \
    --registry-3-router-endpoint "$reg3_router" \
    --consumer-url "$consumer1_url" \
    --consumer-2-url "$consumer2_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --provider-c-url "$provider_c_url" \
    --provider-c-endpoint "$provider_c_endpoint" \
    --registry-main "$REGISTRY_MAIN" \
    --provider-main "$PROVIDER_MAIN" \
    --consumer-main "$CONSUMER_MAIN" \
    --log-dir "$LOG_DIR" \
    --scenario DR-C3 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_d2() {
  local reg_http_port consumer_http_port provider_a_http_port provider_b_http_port
  local reg_pub_port reg_router_port provider_a_channel_port provider_b_channel_port
  reg_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  reg_pub_port="$(pick_port)"
  reg_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local reg_url="http://127.0.0.1:$reg_http_port"
  local consumer_url="http://127.0.0.1:$consumer_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local reg_pub="tcp://127.0.0.1:$reg_pub_port"
  local reg_router="tcp://127.0.0.1:$reg_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg_url" \
    --registry-pub-endpoint "$reg_pub" \
    --registry-router-endpoint "$reg_router" \
    --log-dir "$LOG_DIR"
  wait_health "$reg_url" reg-1

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer "$CONSUMER_MAIN" \
    --http-url "$consumer_url" \
    --registry-router-endpoint "$reg_router" \
    --trace-label consumer-dr-d2 \
    --log-dir "$LOG_DIR"
  wait_health "$consumer_url" consumer

  node "$CLIENT_MAIN" \
    --registry-url "$reg_url" \
    --consumer-url "$consumer_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --scenario DR-D2 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_d1() {
  local embedded_http_port embedded_consumer_http_port embedded_pub_port embedded_router_port embedded_channel_port
  embedded_http_port="$(pick_port)"
  embedded_consumer_http_port="$(pick_port)"
  embedded_pub_port="$(pick_port)"
  embedded_router_port="$(pick_port)"
  embedded_channel_port="$(pick_port)"

  local embedded_url="http://127.0.0.1:$embedded_http_port"
  local embedded_consumer_url="http://127.0.0.1:$embedded_consumer_http_port"
  local embedded_pub="tcp://127.0.0.1:$embedded_pub_port"
  local embedded_router="tcp://127.0.0.1:$embedded_router_port"
  local embedded_channel="tcp://127.0.0.1:$embedded_channel_port"

  start_server embedded-dr-d1 "$EMBEDDED_MAIN" \
    --rid embedded-api \
    --registry-id 10 \
    --http-url "$embedded_url" \
    --registry-pub-endpoint "$embedded_pub" \
    --registry-router-endpoint "$embedded_router" \
    --channel-endpoint "$embedded_channel" \
    --evidence-file "$LOG_DIR/embedded-dr-d1.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$embedded_url" embedded-dr-d1

  start_server consumer-embedded-dr-d1 "$CONSUMER_MAIN" \
    --http-url "$embedded_consumer_url" \
    --registry-router-endpoint "$embedded_router" \
    --trace-label consumer-embedded-dr-d1 \
    --log-dir "$LOG_DIR"
  wait_health "$embedded_consumer_url" consumer-embedded-dr-d1

  node "$CLIENT_MAIN" \
    --registry-url "$embedded_url" \
    --consumer-url "$embedded_consumer_url" \
    --provider-a-url "$embedded_url" \
    --embedded-url "$embedded_url" \
    --embedded-consumer-url "$embedded_consumer_url" \
    --scenario DR-D1 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_d3() {
  local embedded_http_port embedded_consumer_http_port reg1_http_port reg3_http_port
  local provider_a_http_port provider_b_http_port
  local embedded_pub_port reg1_pub_port reg3_pub_port
  local embedded_router_port reg1_router_port reg3_router_port
  local embedded_channel_port provider_a_channel_port provider_b_channel_port
  embedded_http_port="$(pick_port)"
  embedded_consumer_http_port="$(pick_port)"
  reg1_http_port="$(pick_port)"
  reg3_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  embedded_pub_port="$(pick_port)"
  reg1_pub_port="$(pick_port)"
  reg3_pub_port="$(pick_port)"
  embedded_router_port="$(pick_port)"
  reg1_router_port="$(pick_port)"
  reg3_router_port="$(pick_port)"
  embedded_channel_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  local embedded_url="http://127.0.0.1:$embedded_http_port"
  local embedded_consumer_url="http://127.0.0.1:$embedded_consumer_http_port"
  local reg1_url="http://127.0.0.1:$reg1_http_port"
  local reg3_url="http://127.0.0.1:$reg3_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local provider_b_url="http://127.0.0.1:$provider_b_http_port"
  local embedded_pub="tcp://127.0.0.1:$embedded_pub_port"
  local reg1_pub="tcp://127.0.0.1:$reg1_pub_port"
  local reg3_pub="tcp://127.0.0.1:$reg3_pub_port"
  local embedded_router="tcp://127.0.0.1:$embedded_router_port"
  local reg1_router="tcp://127.0.0.1:$reg1_router_port"
  local reg3_router="tcp://127.0.0.1:$reg3_router_port"

  start_server embedded-dr-d3 "$EMBEDDED_MAIN" \
    --rid embedded-api-mixed \
    --registry-id 10 \
    --http-url "$embedded_url" \
    --registry-pub-endpoint "$embedded_pub" \
    --registry-router-endpoint "$embedded_router" \
    --peer "$reg1_pub" \
    --peer "$reg3_pub" \
    --channel-endpoint "tcp://127.0.0.1:$embedded_channel_port" \
    --evidence-file "$LOG_DIR/embedded-dr-d3.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$embedded_url" embedded-dr-d3

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg1_url" \
    --registry-pub-endpoint "$reg1_pub" \
    --registry-router-endpoint "$reg1_router" \
    --peer "$embedded_pub" \
    --peer "$reg3_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg1_url" reg-1

  start_server reg-3 "$REGISTRY_MAIN" \
    --rid reg-3 \
    --registry-id 3 \
    --http-url "$reg3_url" \
    --registry-pub-endpoint "$reg3_pub" \
    --registry-router-endpoint "$reg3_router" \
    --peer "$embedded_pub" \
    --peer "$reg1_pub" \
    --log-dir "$LOG_DIR"
  wait_health "$reg3_url" reg-3

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg1_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$provider_b_url" \
    --registry-router-endpoint "$reg3_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_b_url" api-b

  start_server consumer-embedded-dr-d3 "$CONSUMER_MAIN" \
    --http-url "$embedded_consumer_url" \
    --registry-router-endpoint "$embedded_router" \
    --trace-label consumer-embedded-dr-d3 \
    --log-dir "$LOG_DIR"
  wait_health "$embedded_consumer_url" consumer-embedded-dr-d3

  node "$CLIENT_MAIN" \
    --registry-url "$reg1_url" \
    --consumer-url "$embedded_consumer_url" \
    --provider-a-url "$provider_a_url" \
    --provider-b-url "$provider_b_url" \
    --embedded-url "$embedded_url" \
    --embedded-consumer-url "$embedded_consumer_url" \
    --scenario DR-D3 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

run_dr_d4() {
  local reg_http_port probe_http_port provider_a_http_port
  local reg_pub_port reg_router_port provider_a_channel_port
  reg_http_port="$(pick_port)"
  probe_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  reg_pub_port="$(pick_port)"
  reg_router_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"

  local reg_url="http://127.0.0.1:$reg_http_port"
  local probe_url="http://127.0.0.1:$probe_http_port"
  local provider_a_url="http://127.0.0.1:$provider_a_http_port"
  local reg_pub="tcp://127.0.0.1:$reg_pub_port"
  local reg_router="tcp://127.0.0.1:$reg_router_port"

  start_server reg-1 "$REGISTRY_MAIN" \
    --rid reg-1 \
    --registry-id 1 \
    --http-url "$reg_url" \
    --registry-pub-endpoint "$reg_pub" \
    --registry-router-endpoint "$reg_router" \
    --log-dir "$LOG_DIR"
  wait_health "$reg_url" reg-1

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$provider_a_url" \
    --registry-router-endpoint "$reg_router" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$provider_a_url" api-a

  start_server probe "$PROBE_MAIN" \
    --http-url "$probe_url" \
    --registry-router-endpoint "$reg_router" \
    --log-dir "$LOG_DIR"
  wait_health "$probe_url" probe

  node "$CLIENT_MAIN" \
    --registry-url "$reg_url" \
    --consumer-url "$probe_url" \
    --probe-url "$probe_url" \
    --provider-a-url "$provider_a_url" \
    --scenario DR-D4 \
    >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
}

case "$SCENARIO" in
  DR-A1)
    run_dr_a1
    ;;
  DR-A2)
    run_dr_a2
    ;;
  DR-A3)
    run_dr_a3
    ;;
  DR-A4)
    run_dr_a4
    ;;
  DR-B1)
    run_dr_b1
    ;;
  DR-B2)
    run_dr_b2
    ;;
  DR-B3)
    run_dr_b3
    ;;
  DR-C1)
    run_dr_c1
    ;;
  DR-C2)
    run_dr_c2
    ;;
  DR-C3)
    run_dr_c3
    ;;
  DR-D1)
    run_dr_d1
    ;;
  DR-D2)
    run_dr_d2
    ;;
  DR-D3)
    run_dr_d3
    ;;
  DR-D4)
    run_dr_d4
    ;;
  *)
    echo "Unsupported scenario '$SCENARIO'. Supported: DR-A1, DR-A2, DR-A3, DR-A4, DR-B1, DR-B2, DR-B3, DR-C1, DR-C2, DR-C3, DR-D1, DR-D2, DR-D3, DR-D4" >&2
    exit 2
    ;;
esac

cat "$LOG_DIR/client.stdout.log"
