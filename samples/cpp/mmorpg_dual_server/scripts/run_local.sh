#!/usr/bin/env bash
# run_local.sh — Start all MMORPG sample servers and run the client scenario
#
# Usage:
#   ./scripts/run_local.sh [start|stop|scenario|all]
#
#   start    — Start registry + 2 API servers + 2 front servers in background
#   stop     — Stop all background servers
#   scenario — Run the client scenario runner (servers must be running)
#   all      — Start servers, wait for readiness, run scenario, stop servers

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SAMPLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${SAMPLE_DIR}/build"
PID_DIR="${SAMPLE_DIR}/.pids"

# Server endpoints
REGISTRY_PUB="tcp://127.0.0.1:5550"
REGISTRY_ROUTER="tcp://127.0.0.1:5551"
API1_PORT=6001
API2_PORT=6002
FRONT_A_PORT=7001
FRONT_B_PORT=7002
SPOT_A_PORT=9001
SPOT_B_PORT=9002

start_servers() {
    mkdir -p "$PID_DIR"

    echo "Starting registry..."
    "${BUILD_DIR}/sample-registry" \
        --pub "tcp://*:5550" --router "tcp://*:5551" &
    echo $! > "$PID_DIR/registry.pid"
    sleep 1

    echo "Starting api-1 (port $API1_PORT)..."
    "${BUILD_DIR}/sample-api-server" \
        --id api-1 --port $API1_PORT --registry "$REGISTRY_ROUTER" &
    echo $! > "$PID_DIR/api-1.pid"
    sleep 2

    echo "Starting api-2 (port $API2_PORT)..."
    "${BUILD_DIR}/sample-api-server" \
        --id api-2 --port $API2_PORT --registry "$REGISTRY_ROUTER" &
    echo $! > "$PID_DIR/api-2.pid"
    sleep 2

    echo "Starting front-A zone(0,0) (port $FRONT_A_PORT, SPOT port $SPOT_A_PORT)..."
    "${BUILD_DIR}/sample-front-server" \
        --zone 0 0 --port $FRONT_A_PORT --spot-port $SPOT_A_PORT \
        --registry "$REGISTRY_ROUTER" --registry-pub "$REGISTRY_PUB" &
    echo $! > "$PID_DIR/front-a.pid"
    sleep 2

    echo "Starting front-B zone(1,0) (port $FRONT_B_PORT, SPOT port $SPOT_B_PORT)..."
    "${BUILD_DIR}/sample-front-server" \
        --zone 1 0 --port $FRONT_B_PORT --spot-port $SPOT_B_PORT \
        --registry "$REGISTRY_ROUTER" --registry-pub "$REGISTRY_PUB" &
    echo $! > "$PID_DIR/front-b.pid"

    sleep 3
    echo ""
    echo "All servers started."
    echo "  registry   PUB=$REGISTRY_PUB  ROUTER=$REGISTRY_ROUTER"
    echo "  api-1      port=$API1_PORT"
    echo "  api-2      port=$API2_PORT"
    echo "  front-A    zone(0,0) port=$FRONT_A_PORT  SPOT=$SPOT_A_PORT"
    echo "  front-B    zone(1,0) port=$FRONT_B_PORT  SPOT=$SPOT_B_PORT"
    echo ""
}

stop_servers() {
    if [ -d "$PID_DIR" ]; then
        for pidfile in "$PID_DIR"/*.pid; do
            if [ -f "$pidfile" ]; then
                pid=$(cat "$pidfile")
                name=$(basename "$pidfile" .pid)
                if kill -0 "$pid" 2>/dev/null; then
                    echo "Stopping $name (PID $pid)..."
                    kill "$pid" 2>/dev/null || true
                fi
                rm -f "$pidfile"
            fi
        done
        rmdir "$PID_DIR" 2>/dev/null || true
        echo "All servers stopped."
    else
        echo "No servers running (no PID directory)."
    fi
}

run_scenario() {
    echo "Running client scenario..."
    echo ""
    "${BUILD_DIR}/sample-raw-client" \
        --client "Alice:127.0.0.1:$FRONT_A_PORT" \
        --client "Bob:127.0.0.1:$FRONT_B_PORT" \
        --client "Carol:127.0.0.1:$FRONT_A_PORT"
}

case "${1:-all}" in
    start)
        start_servers
        ;;
    stop)
        stop_servers
        ;;
    scenario)
        run_scenario
        ;;
    all)
        trap stop_servers EXIT
        start_servers
        run_scenario
        ;;
    *)
        echo "Usage: $0 [start|stop|scenario|all]"
        exit 1
        ;;
esac
