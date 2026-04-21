#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
C_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${C_DIR}/../.." && pwd)"
CORE_BUILD_DIR="${ROOT_DIR}/core/build"
BUILD_DIR="${C_DIR}/build"

CONFIGURE_ARGS=(
  -DZLINK_CORE_DIR=${ROOT_DIR}/core
  -DZLINK_C_CORE_BUILD_DIR=${CORE_BUILD_DIR}
  -DZLINK_C_BUILD_SAMPLES=ON
  -DZLINK_C_REGISTER_CTEST=ON
)

if [[ $# -gt 0 ]]; then
  CONFIGURE_ARGS+=("$@")
fi

echo "[c-samples] configure: ${BUILD_DIR}"
cmake -S "${C_DIR}" -B "${BUILD_DIR}" "${CONFIGURE_ARGS[@]}"

echo "[c-samples] build"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

sample_tests=(
  sample_smoke_sample_c_pair_recv_sample
  sample_smoke_sample_c_pubsub_recv_sample
  sample_smoke_sample_c_dealer_router_recv_sample
  sample_smoke_sample_c_stream_recv_sample
  sample_smoke_sample_c_stream_packet_callback_sample
  sample_smoke_sample_c_spot_recv_sample
  sample_smoke_sample_c_monitor_recv_sample
  sample_smoke_sample_c_discovery_registry_sample
  sample_smoke_sample_c_registry_query_sample
)

pass_count=0

for sample_test in "${sample_tests[@]}"; do
  echo "[sample] ${sample_test}"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -R "^${sample_test}$" -j1
  pass_count=$((pass_count + 1))
done

echo "[c-samples] sample summary: ${pass_count}/${#sample_tests[@]} passed"
