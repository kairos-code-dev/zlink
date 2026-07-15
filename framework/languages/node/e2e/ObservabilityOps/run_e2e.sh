#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/log/$RUN_ID"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_TIMEOUT_SECONDS=5
SCENARIO_SETTLE_TIMEOUT_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
mkdir -p "$LOG_DIR"

ATD_OUTPUT="$LOG_DIR/automatic-turn-dispatch.log"
"$NODE_ROOT/e2e/AutomaticTurnDispatch/run_e2e.sh" > >(tee "$ATD_OUTPUT") 2>&1
mapfile -t ATD_LOG_DIRS < <(sed -n 's/^log_dir=//p' "$ATD_OUTPUT")
test "${#ATD_LOG_DIRS[@]}" -gt 0

ATD_FLOW_LOGS=()
ATD_PLAY_FLOW_LOGS=()
ATD_SESSION_FLOW_LOGS=()
for log_dir in "${ATD_LOG_DIRS[@]}"; do
  for flow_log in "$log_dir"/*-flow.log; do
    [[ -f "$flow_log" ]] && ATD_FLOW_LOGS+=("$flow_log")
  done
  [[ -f "$log_dir/play-a-flow.log" ]] && ATD_PLAY_FLOW_LOGS+=("$log_dir/play-a-flow.log")
  [[ -f "$log_dir/session-a-flow.log" ]] && ATD_SESSION_FLOW_LOGS+=("$log_dir/session-a-flow.log")
done
test "${#ATD_FLOW_LOGS[@]}" -gt 0
test "${#ATD_PLAY_FLOW_LOGS[@]}" -gt 0
test "${#ATD_SESSION_FLOW_LOGS[@]}" -gt 0

node --test \
  "$NODE_ROOT/test/contract/runtime-metrics.test.js" \
  "$NODE_ROOT/test/contract/drain-control.test.js" \
  >"$LOG_DIR/contracts.tap"

"$NODE_ROOT/e2e/SpotActorTransfer/run_e2e.sh" >"$LOG_DIR/spot-actor-transfer.log" 2>&1
rg -q 'e2e result=passed' "$LOG_DIR/spot-actor-transfer.log"

stream_flow=""
while read -r candidate; do
  if rg -q "channel=await.delay .*flow=${candidate} " "${ATD_PLAY_FLOW_LOGS[@]}"; then
    stream_flow="$candidate"
    break
  fi
done < <(rg 'packet=AwaitMsg ' "${ATD_SESSION_FLOW_LOGS[@]}" | sed -n 's/.* flow=\([^ ]*\).*/\1/p')
test -n "$stream_flow"
rg -q 'phase=error .*flow=[0-9a-f-]{36} .*origin=' "${ATD_FLOW_LOGS[@]}"
if rg 'message flow' "${ATD_FLOW_LOGS[@]}" | rg -v 'flow=[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12} origin='; then
  echo "ObservabilityOps found a flow line without the mandatory UUIDv7 root." >&2
  exit 1
fi
rg -q 'origin=Timer' "${ATD_PLAY_FLOW_LOGS[@]}"

for scenario in OBS-A1 OBS-A2 OBS-A3 OBS-A4; do
  echo "$scenario flow-correlation PASS flow=$stream_flow"
done
for scenario in OBS-B1 OBS-B2 OBS-B3 OBS-B4; do
  echo "$scenario runtime-metrics PASS"
done
for scenario in OBS-C1 OBS-C2 OBS-C3 OBS-C4 OBS-C5; do
  echo "$scenario graceful-drain PASS"
done
echo "observability-ops e2e result=passed"
