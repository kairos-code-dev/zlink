#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SAMPLES_DIR="$ROOT_DIR/samples"

cd "$ROOT_DIR"

gradle build

for sample in TicTacToe TicTacToe.SessionGateway Bingo StreamingClient; do
  "$SAMPLES_DIR/$sample/run_sample.sh"
done

if rg -n "systems\\.zlink\\.(runtime|internal)|RouteStore|MetadataStore|RemoteAddressResolver|Thread\\.sleep|sleep\\(" "$SAMPLES_DIR" -g '*.java'; then
  echo "sample gate failed: forbidden sample pattern found" >&2
  exit 1
fi

if ! rg -n "attachActorGateway\\(\"session-relay\"\\)" "$SAMPLES_DIR/TicTacToe.SessionGateway" -g '*.java' >/dev/null; then
  echo "sample gate failed: TicTacToe.SessionGateway must attach ActorGateway" >&2
  exit 1
fi

echo "All Java samples passed"
