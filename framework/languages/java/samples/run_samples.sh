#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SAMPLES_DIR="$ROOT_DIR/samples"
CORE_LIB="$ROOT_DIR/../../../core/build/lib/libzlink.so"

if [[ -f "$CORE_LIB" ]]; then
  export ZLINK_LIBRARY_PATH="$CORE_LIB"
fi

cd "$ROOT_DIR"

gradle build

for sample in TicTacToe TicTacToe.SessionGateway Bingo StreamingClient Async; do
  "$SAMPLES_DIR/java/$sample/run_sample.sh"
done

for sample in TicTacToe TicTacToe.SessionGateway Bingo StreamingClient Async; do
  "$SAMPLES_DIR/kotlin/$sample/run_sample.sh"
done

forbidden_sample_pattern="systems\\.zlink\\.(runtime|internal)"
forbidden_sample_pattern+="|systems\\.zlink\\.contracts\\.core\\.RoutingId\\."
forbidden_sample_pattern+="|Route""Store|Metadata""Store|RemoteAddress""Resolver"
forbidden_sample_pattern+="|Thread\\.sleep|sleep\\(|toCompletable""Future\\(\\)"
forbidden_sample_pattern+="|Fake[A-Za-z0-9_]*|Recording[A-Za-z0-9_]*|Catalog"
forbidden_sample_pattern+="|UnsupportedOperationException\\(\"not needed by sample\"\\)"

if rg -n "$forbidden_sample_pattern" "$SAMPLES_DIR" -g '*.java' -g '*.kt'; then
  echo "sample gate failed: forbidden sample pattern found" >&2
  exit 1
fi

if ! rg -n "attachActorGateway\\((\"session-relay\"|SampleNames\\.SessionRelayNode)\\)" "$SAMPLES_DIR/java/TicTacToe.SessionGateway" -g '*.java' >/dev/null; then
  echo "sample gate failed: java/TicTacToe.SessionGateway must attach ActorGateway" >&2
  exit 1
fi

if ! rg -n "attachActorGateway\\((\"session-relay\"|SampleNames\\.SessionRelayNode)\\)" "$SAMPLES_DIR/kotlin/TicTacToe.SessionGateway" -g '*.kt' >/dev/null; then
  echo "sample gate failed: kotlin/TicTacToe.SessionGateway must attach ActorGateway" >&2
  exit 1
fi

echo "All Java/Kotlin samples passed"
