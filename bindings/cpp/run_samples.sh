#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "missing build directory: ${BUILD_DIR}" >&2
  echo "configure/build with bindings samples enabled first" >&2
  exit 1
fi

sample_tests=(
  sample_smoke_sample_cpp_pair_recv
  sample_smoke_sample_cpp_pair_callback
  sample_smoke_sample_cpp_pubsub_recv
  sample_smoke_sample_cpp_pubsub_callback
  sample_smoke_sample_cpp_dealer_router_recv
  sample_smoke_sample_cpp_dealer_router_callback
  sample_smoke_sample_cpp_stream_recv
  sample_smoke_sample_cpp_stream_callback
  sample_smoke_sample_cpp_spot_recv
  sample_smoke_sample_cpp_spot_callback
)

pass_count=0

for sample_test in "${sample_tests[@]}"; do
  echo "[sample] ${sample_test}"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -R "^${sample_test}$" -j1
  pass_count=$((pass_count + 1))
done

echo "sample summary: ${pass_count}/${#sample_tests[@]} passed"
