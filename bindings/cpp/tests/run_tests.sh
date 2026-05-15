#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${CPP_DIR}/../.." && pwd)"
CORE_BUILD_DIR="${ROOT_DIR}/core/build"
BUILD_DIR="${CPP_DIR}/build"

CONFIGURE_ARGS=(
  -DZLINK_CORE_DIR=${ROOT_DIR}/core
  -DZLINK_CPP_CORE_BUILD_DIR=${CORE_BUILD_DIR}
  -DZLINK_CPP_USE_CORE_BUILD_RUNTIME=ON
  -DZLINK_CPP_BUILD_TESTS=ON
  -DZLINK_CPP_BUILD_SAMPLES=OFF
  -DZLINK_CPP_BUILD_BENCHMARKS=OFF
)

TEST_TARGETS=(
  test_cpp_contract_message
  test_cpp_contract_socket
  test_cpp_contract_request_reply
  test_cpp_contract_callback_mode
  test_cpp_contract_options
  test_cpp_contract_monitor
  test_cpp_contract_service
  test_cpp_contract_actor_header
  test_cpp_contract_layout_headers
  test_cpp_contract_behavior
  test_cpp_contract_optimization_guard
)

if [[ $# -gt 0 ]]; then
  CONFIGURE_ARGS+=("$@")
fi

echo "[cpp-tests] configure: ${BUILD_DIR}"
cmake -S "${CPP_DIR}" -B "${BUILD_DIR}" "${CONFIGURE_ARGS[@]}"

echo "[cpp-tests] build"
cmake --build "${BUILD_DIR}" --target "${TEST_TARGETS[@]}" -j"$(nproc)"

echo "[cpp-tests] run contract tests"
if ctest --test-dir "${BUILD_DIR}" --output-on-failure -L contract && \
    "${CPP_DIR}/samples/run_samples.sh"; then
  echo "[cpp-tests] PASS"
else
  echo "[cpp-tests] FAIL"
  exit 1
fi
