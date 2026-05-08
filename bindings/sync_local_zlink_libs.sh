#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_LIB_DIR="${ROOT_DIR}/core/build/lib"
EXPECT_VERSION=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --expect-version)
      EXPECT_VERSION="${2:-}"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "${EXPECT_VERSION}" ]]; then
  echo "--expect-version is required" >&2
  exit 2
fi

case "$(uname -s)" in
  Linux) os="linux" ;;
  Darwin) os="macos" ;;
  *) echo "unsupported OS: $(uname -s)" >&2; exit 2 ;;
esac

case "$(uname -m)" in
  x86_64|amd64) arch_dash="x86_64"; arch_short="x64" ;;
  aarch64|arm64) arch_dash="aarch64"; arch_short="arm64" ;;
  *) echo "unsupported architecture: $(uname -m)" >&2; exit 2 ;;
esac

if [[ "${os}" != "linux" ]]; then
  echo "local sync currently supports Linux shared libraries" >&2
  exit 2
fi

base="${CORE_LIB_DIR}/libzlink.so"
major="${EXPECT_VERSION%%.*}"
soname="${CORE_LIB_DIR}/libzlink.so.${major}"
versioned="${CORE_LIB_DIR}/libzlink.so.${EXPECT_VERSION}"

for f in "${base}" "${soname}" "${versioned}"; do
  if [[ ! -e "${f}" ]]; then
    echo "missing core runtime artifact: ${f}" >&2
    exit 1
  fi
done

copy_libs() {
  local dir="$1"
  mkdir -p "${dir}"
  cp -f "${versioned}" "${dir}/libzlink.so.${EXPECT_VERSION}"
  ln -sfn "libzlink.so.${EXPECT_VERSION}" "${dir}/libzlink.so.${major}"
  ln -sfn "libzlink.so.${major}" "${dir}/libzlink.so"
}

copy_headers() {
  local dir="$1"
  mkdir -p "${dir}"
  cp -f "${ROOT_DIR}/core/include/zlink.h" "${dir}/zlink.h"
  cp -f "${ROOT_DIR}/core/include/zlink_enum.h" "${dir}/zlink_enum.h"
}

copy_headers "${ROOT_DIR}/bindings/c/include"
copy_headers "${ROOT_DIR}/bindings/cpp/include"
copy_headers "${ROOT_DIR}/bindings/go/include"
copy_headers "${ROOT_DIR}/bindings/rust/include"

copy_libs "${ROOT_DIR}/bindings/cpp/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/cpp/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/dotnet/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/dotnet/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/dotnet/runtimes/linux-${arch_short}/native"
copy_libs "${ROOT_DIR}/bindings/go/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/go/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/java/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/java/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/java/src/main/resources/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/java/src/main/resources/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/node/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/node/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/node/prebuilds/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/python/src/zlink/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/python/src/zlink/native/linux-${arch_dash}"
copy_libs "${ROOT_DIR}/bindings/rust/native/linux-${arch_short}"
copy_libs "${ROOT_DIR}/bindings/rust/native/linux-${arch_dash}"

check_program="$(mktemp)"
trap 'rm -f "${check_program}" "${check_program}.c"' EXIT
cat >"${check_program}.c" <<'C'
#include <stdio.h>
#include "zlink.h"
int main(void) {
  int major = 0, minor = 0, patch = 0;
  zlink_version(&major, &minor, &patch);
  printf("%d.%d.%d\n", major, minor, patch);
  return 0;
}
C
cc -I"${ROOT_DIR}/core/include" "${check_program}.c" \
  -L"${ROOT_DIR}/bindings/cpp/native/linux-${arch_dash}" -lzlink \
  -Wl,-rpath,"${ROOT_DIR}/bindings/cpp/native/linux-${arch_dash}" \
  -o "${check_program}"
actual="$("${check_program}")"
if [[ "${actual}" != "${EXPECT_VERSION}" ]]; then
  echo "runtime version mismatch: expected ${EXPECT_VERSION}, got ${actual}" >&2
  exit 1
fi

echo "synced libzlink ${EXPECT_VERSION} for ${os}-${arch_short}"
