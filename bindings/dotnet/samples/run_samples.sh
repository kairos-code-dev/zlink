#!/usr/bin/env bash
set -uo pipefail

ROOT="/home/hep7/project/kairos/zlink/bindings/dotnet/samples"
VERSION_FILE="/home/hep7/project/kairos/zlink/VERSION"
CORE_LIB_DIR="/home/hep7/project/kairos/zlink/core/build/lib"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so.${CORE_VERSION}"

if [[ -f "$CORE_LIB" ]]; then
  export ZLINK_LIBRARY_PATH="$CORE_LIB"
fi

sync_native_dirs() {
  local search_root="$1"
  [[ -d "$search_root" ]] || return 0

  while IFS= read -r native_dir; do
    rm -f "${native_dir}/libzlink.so" \
      "${native_dir}/libzlink.so.6" \
      "${native_dir}/libzlink.so."*
    cp -f "$CORE_LIB" "${native_dir}/libzlink.so.${CORE_VERSION}"
    ln -sfn "libzlink.so.${CORE_VERSION}" "${native_dir}/libzlink.so.6"
    ln -sfn libzlink.so.6 "${native_dir}/libzlink.so"
  done < <(find "$search_root" -type d -path '*linux-x64/native')
}

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
  "ActorRoomServer/ActorRoomServer.csproj"
  "ActorGatewayRelay/ActorGatewayRelay.csproj"
  "ActorSinglePlayerQueue/ActorSinglePlayerQueue.csproj"
  "DiscoveryRegistry/DiscoveryRegistry.csproj"
  "RegistryQuery/RegistryQuery.csproj"
)

passed=0
failed=0

for sample in "${SAMPLES[@]}"; do
  echo "RUN,$sample"
  if dotnet build "$ROOT/$sample" && { [[ ! -f "$CORE_LIB" ]] || sync_native_dirs "$(dirname "$ROOT/$sample")/bin"; } && \
      dotnet run --no-build --project "$ROOT/$sample"; then
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
