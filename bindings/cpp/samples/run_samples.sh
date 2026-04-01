#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${CPP_DIR}/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

CONFIGURE_ARGS=(
  -DZLINK_BUILD_CPP_BINDINGS=ON
  -DZLINK_CPP_BUILD_SAMPLES=ON
)

if [[ $# -gt 0 ]]; then
  CONFIGURE_ARGS+=("$@")
fi

echo "[cpp-samples] configure: ${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${CONFIGURE_ARGS[@]}"

echo "[cpp-samples] build"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[cpp-samples] run sample smoke tests"
if ctest --test-dir "${BUILD_DIR}" --output-on-failure -L sample-smoke; then
  echo "[cpp-samples] PASS"
else
  echo "[cpp-samples] FAIL"
  exit 1
fi
