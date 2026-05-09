#!/usr/bin/env bash
#
# dev_sync_local_core_libs.sh — copy a locally built core/build/lib/libzlink.so*
# into every binding's native directory so binding-side perf tests, samples,
# and contract tests pick up an in-progress core change without waiting for
# a GitHub release.
#
# This is for local development only. The committed contents of
# bindings/*/native/ are produced by the release CI from a tagged version of
# the core library; do NOT commit the files written by this script. Run
# `git restore bindings/*/native/libzlink.so*` (or equivalent) before staging
# unrelated changes for commit.
#
# Usage:
#   bindings/dev_sync_local_core_libs.sh
#
# Optional environment overrides:
#   CORE_LIB_DIR   — directory containing libzlink.so/libzlink.so.MAJOR/
#                    libzlink.so.MAJOR.MINOR.PATCH (default: core/build/lib).
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_LIB_DIR="${CORE_LIB_DIR:-${ROOT_DIR}/core/build/lib}"

case "$(uname -s)" in
  Linux) os="linux" ;;
  Darwin) os="macos" ;;
  *) echo "unsupported OS: $(uname -s)" >&2; exit 2 ;;
esac
if [[ "${os}" != "linux" ]]; then
  echo "this dev sync currently only handles Linux shared libraries" >&2
  exit 2
fi

case "$(uname -m)" in
  x86_64|amd64) arch_dash="x86_64"; arch_short="x64" ;;
  aarch64|arm64) arch_dash="aarch64"; arch_short="arm64" ;;
  *) echo "unsupported architecture: $(uname -m)" >&2; exit 2 ;;
esac

VERSION="$(bash "${ROOT_DIR}/core/version.sh")"
MAJOR="${VERSION%%.*}"

base="${CORE_LIB_DIR}/libzlink.so"
soname="${CORE_LIB_DIR}/libzlink.so.${MAJOR}"
versioned="${CORE_LIB_DIR}/libzlink.so.${VERSION}"

for f in "${base}" "${soname}" "${versioned}"; do
  if [[ ! -e "${f}" ]]; then
    echo "missing core artifact: ${f}" >&2
    echo "build core first (e.g. cmake --build core/build)." >&2
    exit 1
  fi
done

copy_libs() {
  local dir="$1"
  if [[ ! -d "${dir}" ]]; then
    return
  fi
  cp -f "${versioned}" "${dir}/libzlink.so.${VERSION}"
  ln -sfn "libzlink.so.${VERSION}" "${dir}/libzlink.so.${MAJOR}"
  ln -sfn "libzlink.so.${MAJOR}" "${dir}/libzlink.so"
}

dirs=(
  "${ROOT_DIR}/bindings/cpp/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/cpp/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/dotnet/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/dotnet/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/dotnet/runtimes/linux-${arch_short}/native"
  "${ROOT_DIR}/bindings/go/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/go/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/java/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/java/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/java/src/main/resources/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/java/src/main/resources/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/node/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/node/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/node/prebuilds/linux-${arch_short}"
  "${ROOT_DIR}/bindings/python/src/zlink/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/python/src/zlink/native/linux-${arch_dash}"
  "${ROOT_DIR}/bindings/rust/native/linux-${arch_short}"
  "${ROOT_DIR}/bindings/rust/native/linux-${arch_dash}"
)

for dir in "${dirs[@]}"; do
  copy_libs "${dir}"
done

echo "synced libzlink ${VERSION} from ${CORE_LIB_DIR} into binding native dirs"
echo
echo "WARNING: bindings/*/native/libzlink.so* are release artifacts. Do NOT"
echo "commit the files written by this script. Run"
echo "  git restore bindings/*/native/libzlink.so* bindings/*/src/**/native/libzlink.so* bindings/*/prebuilds/*/libzlink.so* bindings/*/runtimes/*/native/libzlink.so*"
echo "before staging unrelated changes."
