#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${ROOT}/../../.." && pwd)"
VERSION_FILE="${REPO_ROOT}/VERSION"
CORE_LIB_DIR="${REPO_ROOT}/core/build/lib"
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

mapfile -t SAMPLES < <(
  dotnet sln "${ROOT}/Zlink.Samples.sln" list |
    awk '/\.csproj$/ && $0 !~ /^SampleCommon\// { print }'
)

if (( ${#SAMPLES[@]} == 0 )); then
  echo "No sample projects found in Zlink.Samples.sln" >&2
  exit 1
fi

passed=0
failed=0

for sample in "${SAMPLES[@]}"; do
  echo "RUN,$sample"
  if dotnet build "$ROOT/$sample" && { [[ ! -f "$CORE_LIB" ]] || sync_native_dirs "$(dirname "$ROOT/$sample")/bin"; } && \
      timeout 60s dotnet run --no-build --project "$ROOT/$sample"; then
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
