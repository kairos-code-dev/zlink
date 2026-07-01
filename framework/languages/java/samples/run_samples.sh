#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SAMPLES_DIR="$ROOT_DIR/samples"
CORE_LIB="$ROOT_DIR/../../../core/build/lib/libzlink.so"

if [[ -f "$CORE_LIB" ]]; then
  export ZLINK_LIBRARY_PATH="$CORE_LIB"
fi

cd "$ROOT_DIR"

SAMPLE_FILTER="${ZLINK_SAMPLE_FILTER:-}"

should_run_sample() {
  local language="$1"
  local sample="$2"
  [[ -z "$SAMPLE_FILTER" || "$SAMPLE_FILTER" == "$sample" || "$SAMPLE_FILTER" == "$language/$sample" ]]
}

./gradlew -Pzlink.useLocalBindings=true --no-daemon \
  :zlink-framework-testkit:contractTest \
  --tests '*SampleReleaseGateContractTest*'

./gradlew -Pzlink.useLocalBindings=true --no-daemon --no-parallel \
  :zlink-framework-testkit:fakeBackendTest \
  --tests 'systems.zlink.framework.testkit.ActorRuntimeFakeBackendTest.entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback'
echo "java actor lifecycle sample gate completed"

run_sample_with_retry() {
  local script="$1"
  local attempt
  local output
  output="$(mktemp)"
  for attempt in 1 2 3; do
    if "$script" >"$output" 2>&1; then
      cat "$output"
      rm -f "$output"
      return 0
    fi
    if ! rg -q "ZlinkBindException|Timed out waiting" "$output"; then
      cat "$output" >&2
      rm -f "$output"
      return 1
    fi
    if [[ "$attempt" == "3" ]]; then
      cat "$output" >&2
      rm -f "$output"
      return 1
    fi
    echo "sample transient port bind failure; retrying ${script} (${attempt}/3)" >&2
  done
}

for sample in TicTacToe Bingo; do
  if should_run_sample java "$sample"; then
    run_sample_with_retry "$SAMPLES_DIR/java/$sample/run_sample.sh"
  fi
done

for sample in TicTacToe Bingo; do
  if should_run_sample kotlin "$sample"; then
    run_sample_with_retry "$SAMPLES_DIR/kotlin/$sample/run_sample.sh"
  fi
done

if should_run_sample kotlin DeliveryDispatch; then
  run_sample_with_retry "$SAMPLES_DIR/kotlin/DeliveryDispatch/run_sample.sh"
fi

forbidden_sample_pattern="systems\\.zlink\\.(runtime|internal)"
forbidden_sample_pattern+="|systems\\.zlink\\.contracts\\.core\\.RoutingId\\."
forbidden_sample_pattern+="|Metadata""Store"
forbidden_sample_pattern+="|Thread\\.sleep|sleep\\(|CountDownLatch|toCompletable""Future\\(\\)"
forbidden_sample_pattern+="|ZLinkFrameworkRuntime\\.start"
forbidden_sample_pattern+="|Fake[A-Za-z0-9_]*|Recording[A-Za-z0-9_]*|Catalog"
forbidden_sample_pattern+="|UnsupportedOperationException\\(\"not needed by sample\"\\)"

if rg -n "$forbidden_sample_pattern" "$SAMPLES_DIR" -g '*.java' -g '*.kt'; then
  echo "sample gate failed: forbidden sample pattern found" >&2
  exit 1
fi

echo "All Java/Kotlin samples passed"
