#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98"

cd "${NODE_ROOT}"
npm run build >/dev/null
env -u NODE_TEST_CONTEXT node --test \
  --test-name-pattern 'ZLinkEntrySpotActivation destroyActor does not invoke Entry Spot lifecycle callbacks' \
  test/contract/actor-manager.test.js
echo "node actor lifecycle sample gate completed"

declare -A SAMPLE_SCRIPTS=(
  [TicTacToe.Ts]="${SCRIPT_DIR}/TicTacToe.Ts/run_sample.sh"
  [Bingo.Ts]="${SCRIPT_DIR}/Bingo.Ts/run_sample.sh"
  [DeliveryDispatch.Ts]="${SCRIPT_DIR}/DeliveryDispatch.Ts/run_sample.sh"
  [SupportChat.Ts]="${SCRIPT_DIR}/SupportChat.Ts/run_sample.sh"
  [GameQuest.Ts]="${SCRIPT_DIR}/GameQuest.Ts/run_sample.sh"
  [ShoppingMall.Ts]="${SCRIPT_DIR}/ShoppingMall.Ts/run_sample.sh"
)

DEFAULT_SAMPLES=(
  TicTacToe.Ts
  Bingo.Ts
  DeliveryDispatch.Ts
  SupportChat.Ts
  GameQuest.Ts
  ShoppingMall.Ts
)

run_required_sample() {
  local name="$1"
  local script="$2"
  local attempt output status
  output="$(mktemp)"
  for attempt in 1 2 3; do
  echo "sample ${name} start"
    : >"${output}"
    set +e
    "${script}" 2>&1 | tee "${output}"
    status="${PIPESTATUS[0]}"
    set -e
    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      echo "sample ${name} completed"
      return 0
    fi
    if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
      rm -f "${output}"
      echo "sample ${name} failed with status ${status}" >&2
      return "${status}"
    fi
    if [[ "${attempt}" == "3" ]]; then
      rm -f "${output}"
      echo "sample ${name} failed with status ${status}" >&2
      return "${status}"
    fi
    echo "node sample transient bind failure; retrying ${name} (${attempt}/3)" >&2
    sleep 1
  done
}

if [[ "$#" -eq 0 ]]; then
  selected_samples=("${DEFAULT_SAMPLES[@]}")
else
  selected_samples=("$@")
fi

for sample in "${selected_samples[@]}"; do
  if [[ -z "${SAMPLE_SCRIPTS[$sample]:-}" ]]; then
    echo "Unknown node sample '${sample}'." >&2
    echo "Known samples: ${DEFAULT_SAMPLES[*]}" >&2
    exit 1
  fi
  run_required_sample "${sample}" "${SAMPLE_SCRIPTS[$sample]}"
done
