#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
expected_seed="20260715"
configs=(
  AutomaticTurnDispatch
  ObservabilityOps
  PubSub
  RegistrationCodec
  RegistryMessaging
  ResilienceLifecycle
  RuntimeMonitoring
  SpotActorTransfer
  SpotService
  StoreFailure
  ToActorMessaging
)

for config in "${configs[@]}"; do
  runner="${script_dir}/${config}/run_e2e.sh"
  if ! rg -q 'zlink_e2e_order_roles' "${runner}"; then
    echo "${config} runner does not apply the shared server start-order policy" >&2
    exit 1
  fi
done

aggregate="${script_dir}/run_e2e_all.sh"
for mode in reverse "shuffle:${expected_seed}"; do
  if ! rg -Fq "${mode}" "${aggregate}"; then
    echo "aggregate runner does not execute the ${mode} start-order axis" >&2
    exit 1
  fi
done

for config in RegistryMessaging SpotService ToActorMessaging; do
  if ! rg -q "START_ORDER_CONFIGS=.*${config}|${config}.*START_ORDER_CONFIGS" "${aggregate}"; then
    echo "aggregate runner does not include ${config} in the start-order axis" >&2
    exit 1
  fi
done

echo "java e2e start-order contract gate passed"
