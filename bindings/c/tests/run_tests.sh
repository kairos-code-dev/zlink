#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
C_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${C_DIR}/../.." && pwd)"
BUILD_DIR="${C_DIR}/build"

cmake -S "${C_DIR}" -B "${BUILD_DIR}" \
  -DZLINK_CORE_DIR="${ROOT_DIR}/core" \
  -DZLINK_C_CORE_BUILD_DIR="${ROOT_DIR}/core/build" \
  -DZLINK_C_BUILD_TESTS=ON

cmake --build "${BUILD_DIR}" --target test_c_contract_surface -j"$(nproc)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure -L contract
