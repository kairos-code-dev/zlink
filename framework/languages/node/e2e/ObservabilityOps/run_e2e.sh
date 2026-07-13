#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

ATD_OUTPUT="$LOG_DIR/automatic-turn-dispatch.log"
"$NODE_ROOT/e2e/AutomaticTurnDispatch/run_e2e.sh" > >(tee "$ATD_OUTPUT") 2>&1
ATD_LOG_DIR="$(sed -n 's/^log_dir=//p' "$ATD_OUTPUT" | head -1)"
test -n "$ATD_LOG_DIR"

node --test \
  "$NODE_ROOT/test/contract/runtime-metrics.test.js" \
  "$NODE_ROOT/test/contract/drain-control.test.js" \
  >"$LOG_DIR/contracts.tap"

"$NODE_ROOT/e2e/SpotActorTransfer/run_e2e.sh" >"$LOG_DIR/spot-actor-transfer.log" 2>&1
rg -q 'e2e result=passed' "$LOG_DIR/spot-actor-transfer.log"

stream_flow=""
while read -r candidate; do
  if rg -q "channel=await.delay .*flow=${candidate} " "$ATD_LOG_DIR/play-a-flow.log"; then
    stream_flow="$candidate"
    break
  fi
done < <(rg 'packet=AwaitMsg ' "$ATD_LOG_DIR/session-a-flow.log" | sed -n 's/.* flow=\([^ ]*\).*/\1/p')
test -n "$stream_flow"
rg -q 'phase=error .*flow=[0-9a-f-]{36} .*origin=' "$ATD_LOG_DIR"/*-flow.log
if rg 'message flow' "$ATD_LOG_DIR"/*-flow.log | rg -v 'flow=[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12} origin='; then
  echo "ObservabilityOps found a flow line without the mandatory UUIDv7 root." >&2
  exit 1
fi
rg -q 'origin=Timer' "$ATD_LOG_DIR"/play-a-flow.log

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
