#!/usr/bin/env bash
set -uo pipefail

ROOT="/home/hep7/project/kairos/zlink/bindings/dotnet/samples"
CORE_LIB="/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.2"

if [[ -f "$CORE_LIB" ]]; then
  export ZLINK_LIBRARY_PATH="$CORE_LIB"
  while IFS= read -r native_dir; do
    cp -f "$CORE_LIB" "${native_dir}/libzlink.so.5.3.2"
    ln -sfn libzlink.so.5.3.2 "${native_dir}/libzlink.so.5"
    ln -sfn libzlink.so.5 "${native_dir}/libzlink.so"
  done < <(find "/home/hep7/project/kairos/zlink/bindings/dotnet" -type d -path '*linux-x64/native')
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
