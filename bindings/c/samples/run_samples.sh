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
  -DZLINK_C_BUILD_TESTS=OFF
  -DZLINK_C_BUILD_BENCHMARKS=OFF
  -DZLINK_C_BUILD_BENCHES=OFF
  -DZLINK_BUILD_BENCH_ZMQ=OFF
  -DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF
  -DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF
)

if [[ $# -gt 0 ]]; then
  CONFIGURE_ARGS+=("$@")
fi

echo "[c-samples] configure: ${BUILD_DIR}"
cmake -S "${C_DIR}" -B "${BUILD_DIR}" "${CONFIGURE_ARGS[@]}"

sample_bins=(
  sample_c_pair_recv_sample
  sample_c_pubsub_recv_sample
  sample_c_dealer_router_recv_sample
  sample_c_stream_recv_sample
  sample_c_stream_packet_callback_sample
  sample_c_spot_recv_sample
  sample_c_spot_routed_request_sample
  sample_c_monitor_recv_sample
  sample_c_discovery_registry_sample
  sample_c_registry_query_sample
  sample_c_actor_room_server_sample
  sample_c_actor_gateway_relay_sample
  sample_c_actor_single_player_queue_sample
)

echo "[c-samples] build"
cmake --build "${BUILD_DIR}" --target "${sample_bins[@]}" -j"$(nproc)"

pass_count=0

for sample_bin in "${sample_bins[@]}"; do
  echo "[sample] ${sample_bin}"
  (cd "${ROOT_DIR}" && "${BUILD_DIR}/samples/${sample_bin}")
  pass_count=$((pass_count + 1))
done

echo "[c-samples] sample summary: ${pass_count}/${#sample_bins[@]} passed"
