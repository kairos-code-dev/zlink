#!/usr/bin/env bash
set -uo pipefail

ROOT="/home/hep7/project/kairos/zlink/bindings/dotnet/samples"
CORE_LIB="/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so"

if [[ -f "$CORE_LIB" ]]; then
  export ZLINK_LIBRARY_PATH="$CORE_LIB"
fi

SAMPLES=(
  "RequestReplyAsync/RequestReplyAsync.csproj"
  "RequestReplyCallback/RequestReplyCallback.csproj"
  "PairRecv/PairRecv.csproj"
  "PairCallback/PairCallback.csproj"
  "MonitorRecv/MonitorRecv.csproj"
  "PubSubRecv/PubSubRecv.csproj"
  "PubSubCallback/PubSubCallback.csproj"
  "DealerRouterRecv/DealerRouterRecv.csproj"
  "DealerRouterCallback/DealerRouterCallback.csproj"
  "StreamRecv/StreamRecv.csproj"
  "StreamCallback/StreamCallback.csproj"
  "SpotRecv/SpotRecv.csproj"
  "SpotCallback/SpotCallback.csproj"
)

passed=0
failed=0

for sample in "${SAMPLES[@]}"; do
  echo "RUN,$sample"
  if dotnet run --project "$ROOT/$sample"; then
    echo "OK,$sample"
    passed=$((passed + 1))
  else
    echo "FAIL,$sample"
    failed=$((failed + 1))
  fi
done

echo "SUMMARY,passed,$passed,failed,$failed,total,${#SAMPLES[@]}"

if (( failed > 0 )); then
  exit 1
fi
