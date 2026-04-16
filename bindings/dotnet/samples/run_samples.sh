#!/usr/bin/env bash
set -uo pipefail

ROOT="/home/hep7/project/kairos/zlink/bindings/dotnet/samples"
CORE_LIB="/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so"

if [[ -f "$CORE_LIB" ]]; then
  export ZLINK_LIBRARY_PATH="$CORE_LIB"
fi

SAMPLES=(
  "RequestReplyAsync/RequestReplyAsync.csproj"
  "PairRecv/PairRecv.csproj"
  "MonitorRecv/MonitorRecv.csproj"
  "PubSubRecv/PubSubRecv.csproj"
  "DealerRouterRecv/DealerRouterRecv.csproj"
  "StreamRecv/StreamRecv.csproj"
  "StreamPacketCallback/StreamPacketCallback.csproj"
  "SpotRecv/SpotRecv.csproj"
  "SpotRequestAsync/SpotRequestAsync.csproj"
  "DiscoveryRegistry/DiscoveryRegistry.csproj"
  "RegistryQuery/RegistryQuery.csproj"
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
