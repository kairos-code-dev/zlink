#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"

echo "=== zlink Rust binding samples ==="
echo ""

source "$HOME/.cargo/env" 2>/dev/null || true

PASS=0
FAIL=0
TOTAL=0

run_sample() {
    local name="$1"
    TOTAL=$((TOTAL + 1))
    echo -n "  $name ... "
    if timeout 30 cargo run --example "$name" 2>&1; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
    fi
}

echo "Running samples:"
echo ""

run_sample pair_recv_sample
run_sample spot_recv_sample
run_sample request_reply_async_sample
run_sample pubsub_recv_sample
run_sample dealer_router_recv_sample
run_sample stream_recv_sample
run_sample stream_packet_callback_sample
run_sample spot_request_async_sample
run_sample monitor_recv_sample
run_sample discovery_registry_sample
run_sample registry_query_sample
run_sample actor_room_server_sample
run_sample actor_gateway_relay_sample
run_sample actor_single_player_queue_sample

echo ""
echo "=== Summary ==="
echo "  Total:  $TOTAL"
echo "  Pass:   $PASS"
echo "  Fail:   $FAIL"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo "RESULT: FAIL"
    exit 1
else
    echo "RESULT: PASS"
    exit 0
fi
