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

sample_tests=(
  sample_smoke_sample_cpp_pair_recv_sample
  sample_smoke_sample_cpp_pair_callback_sample
  sample_smoke_sample_cpp_pubsub_recv_sample
  sample_smoke_sample_cpp_pubsub_callback_sample
  sample_smoke_sample_cpp_dealer_router_recv_sample
  sample_smoke_sample_cpp_dealer_router_callback_sample
  sample_smoke_sample_cpp_stream_recv_sample
  sample_smoke_sample_cpp_stream_callback_sample
  sample_smoke_sample_cpp_spot_recv_sample
  sample_smoke_sample_cpp_spot_callback_sample
  sample_smoke_sample_cpp_monitor_recv_sample
)

pass_count=0

for sample_test in "${sample_tests[@]}"; do
  echo "[sample] ${sample_test}"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -R "^${sample_test}$" -j1
  pass_count=$((pass_count + 1))
done

echo "[cpp-samples] sample summary: ${pass_count}/${#sample_tests[@]} passed"
