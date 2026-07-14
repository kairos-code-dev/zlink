#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"
SCENARIO="${1:-all}"
SCENARIO_LOWER="$(printf '%s' "$SCENARIO" | tr '[:upper:]' '[:lower:]')"
case "$SCENARIO_LOWER" in
  all|rc-a[1-6]|rc-b[1-5]|b5) ;;
  *)
    echo "Unsupported RegistrationCodec scenario: $SCENARIO" >&2
    exit 2
    ;;
esac
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"

read -r API HTTP JSON_ONLY_API JSON_ONLY_HTTP INVALID REQUESTER_HTTP <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(6):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(f"tcp://127.0.0.1:{ports[0]}", end=" ")
print(f"http://127.0.0.1:{ports[1]}", end=" ")
print(f"tcp://127.0.0.1:{ports[2]}", end=" ")
print(f"http://127.0.0.1:{ports[3]}", end=" ")
print(f"tcp://127.0.0.1:{ports[4]}", end=" ")
print(f"http://127.0.0.1:{ports[5]}")
for s in sockets:
    s.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_registration_codec_server \
  zlink_cpp_e2e_registration_codec_invalid_duplicate \
  zlink_cpp_e2e_registration_codec_json_only_peer \
  zlink_cpp_e2e_registration_codec_codec_requester \
  zlink_cpp_e2e_registration_codec_client >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_registration_codec_server"
INVALID_SERVER="$BUILD_DIR/zlink_cpp_e2e_registration_codec_invalid_duplicate"
JSON_ONLY_SERVER="$BUILD_DIR/zlink_cpp_e2e_registration_codec_json_only_peer"
CODEC_REQUESTER="$BUILD_DIR/zlink_cpp_e2e_registration_codec_codec_requester"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_registration_codec_client"
PIDS=()

cleanup() {
  local code=$?
  local cleanup_failed=0
  local status
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  for pid in "${PIDS[@]:-}"; do
    set +e
    wait "$pid" >/dev/null 2>&1
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "cleanup process $pid exited unexpectedly with status $status" >&2
      cleanup_failed=1
    fi
  done
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  elif [[ "$cleanup_failed" -ne 0 ]]; then
    echo "E2E cleanup failed. Logs: $LOG_DIR" >&2
    code=1
  fi
  exit "$code"
}
trap cleanup EXIT

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host="127.0.0.1"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $endpoint" >&2
  return 1
}

ZLINK_CPP_E2E_API_ENDPOINT="$API" \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$SERVER" >"$LOG_DIR/server.stdout.log" 2>"$LOG_DIR/server.stderr.log" &
PIDS+=("$!")
wait_port api "$API"
wait_port http "$HTTP"

ZLINK_CPP_E2E_API_ENDPOINT="$JSON_ONLY_API" \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$JSON_ONLY_HTTP" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$JSON_ONLY_SERVER" >"$LOG_DIR/json-only.stdout.log" 2>"$LOG_DIR/json-only.stderr.log" &
PIDS+=("$!")
wait_port json-only-api "$JSON_ONLY_API"
wait_port json-only-http "$JSON_ONLY_HTTP"

ZLINK_CPP_E2E_API_ENDPOINT="$JSON_ONLY_API" \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$REQUESTER_HTTP" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CODEC_REQUESTER" >"$LOG_DIR/codec-requester.stdout.log" 2>"$LOG_DIR/codec-requester.stderr.log" &
PIDS+=("$!")
wait_port codec-requester-http "$REQUESTER_HTTP"

sleep "$ROUTE_SETTLE_SECONDS"

if [[ "$SCENARIO_LOWER" == "all" || "$SCENARIO_LOWER" == rc-a[1-6] || "$SCENARIO_LOWER" == rc-b[1-4] ]]; then
  ZLINK_CPP_E2E_SCENARIO="$SCENARIO_LOWER" \
  ZLINK_CPP_E2E_API_ENDPOINT="$API" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP" \
  ZLINK_CPP_E2E_INVALID_SERVER_EXE="$INVALID_SERVER" \
  ZLINK_CPP_E2E_INVALID_ENDPOINT="$INVALID" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
  cat "$LOG_DIR/client.stdout.log"
fi

if [[ "$SCENARIO_LOWER" == "all" || "$SCENARIO_LOWER" == "rc-b5" || "$SCENARIO_LOWER" == "b5" ]]; then
  ZLINK_CPP_E2E_SCENARIO="rc-b5" \
ZLINK_CPP_E2E_API_ENDPOINT="$JSON_ONLY_API" \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$REQUESTER_HTTP" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CLIENT" >"$LOG_DIR/client-b5.stdout.log" 2>"$LOG_DIR/client-b5.stderr.log"
  cat "$LOG_DIR/client-b5.stdout.log"
fi

echo "registration-codec e2e result=passed"
