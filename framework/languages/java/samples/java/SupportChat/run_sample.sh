#!/usr/bin/env bash
set -euo pipefail
set +m

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

LOG_DIR="$ROOT_DIR/logs"
BUILD_LOG="$LOG_DIR/build.log"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.log

export SUPPORTCHAT_LOG_DIR="$LOG_DIR"
export SUPPORTCHAT_API_CHANNEL_ENDPOINT="${SUPPORTCHAT_API_CHANNEL_ENDPOINT:-tcp://127.0.0.1:19611}"
export SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT="${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT:-tcp://127.0.0.1:19612}"
export SUPPORTCHAT_SESSION_STREAM_ENDPOINT="${SUPPORTCHAT_SESSION_STREAM_ENDPOINT:-tcp://127.0.0.1:19613}"
export SUPPORTCHAT_SESSION_SPOT_ROUTER_ENDPOINT="${SUPPORTCHAT_SESSION_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:19614}"
export SUPPORTCHAT_SUPPORT_SPOT_ROUTER_ENDPOINT="${SUPPORTCHAT_SUPPORT_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:19615}"
export SUPPORTCHAT_API_HTTP_ENDPOINT="${SUPPORTCHAT_API_HTTP_ENDPOINT:-http://127.0.0.1:19621}"
export SUPPORTCHAT_SUPPORT_HTTP_ENDPOINT="${SUPPORTCHAT_SUPPORT_HTTP_ENDPOINT:-http://127.0.0.1:19622}"
export SUPPORTCHAT_REDIS_KEY_PREFIX="${SUPPORTCHAT_REDIS_KEY_PREFIX:-zlink:supportchat:sample:$(date +%s):$$}"

REDIS_CONTAINER=""
if [[ -z "${SUPPORTCHAT_REDIS_ENDPOINT:-}" ]]; then
  REDIS_CONTAINER="$(zlink_redis_start_scoped "zlink-redis-java-sample" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}" "127.0.0.1::6379")"
  REDIS_PORT="$(zlink_redis_host_port "$REDIS_CONTAINER")"
  export SUPPORTCHAT_REDIS_ENDPOINT="127.0.0.1:$REDIS_PORT"
fi

pids=()
trap cleanup EXIT

cd "$ROOT_DIR"
../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon \
  :Server:Api:installDist \
  :Server:Session:installDist \
  :Server:Support:installDist \
  :Client:installDist >"$BUILD_LOG" 2>&1

start_role() {
  local name="$1"
  local binary="$2"
  "$binary" >"$LOG_DIR/$name.log" 2>&1 &
  pids+=("$!")
}

start_role support "$ROOT_DIR/Server/Support/build/install/Support/bin/Support"
start_role api "$ROOT_DIR/Server/Api/build/install/Api/bin/Api"
start_role session "$ROOT_DIR/Server/Session/build/install/Session/bin/Session"
disown "${pids[@]}" 2>/dev/null || true

wait_http "$SUPPORTCHAT_SUPPORT_HTTP_ENDPOINT"
wait_http "$SUPPORTCHAT_API_HTTP_ENDPOINT"
echo "topology=ready"

"$ROOT_DIR/Client/build/install/Client/bin/Client" >"$LOG_DIR/client.log" 2>&1
cat "$LOG_DIR/client.log"

conversation_id="$(sed -n 's/^supportchat-conversation=//p' "$LOG_DIR/client.log" | tail -1)"
if [[ -z "$conversation_id" ]]; then
  echo "missing supportchat conversation marker" >&2
  exit 1
fi

assertion="$(curl -fsS "$SUPPORTCHAT_SUPPORT_HTTP_ENDPOINT/self-check/assert/$conversation_id")"
if [[ "$assertion" != *'"passed":true'* ]]; then
  echo "supportchat server assertion failed: $assertion" >&2
  exit 1
fi

for file in "$LOG_DIR"/flow-*.log; do
  if [[ ! -s "$file" ]]; then
    echo "missing message flow log: $file" >&2
    exit 1
  fi
done

echo "supportchat-server-evidence=completed"
echo "supportchat full client/server self-check completed"
