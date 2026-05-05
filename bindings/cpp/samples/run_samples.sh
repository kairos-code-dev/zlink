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
  -DZLINK_CPP_BUILD_SAMPLES=ON
)

if [[ $# -gt 0 ]]; then
  CONFIGURE_ARGS+=("$@")
fi

echo "[cpp-samples] configure: ${BUILD_DIR}"
cmake -S "${CPP_DIR}" -B "${BUILD_DIR}" "${CONFIGURE_ARGS[@]}"

echo "[cpp-samples] build"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

sample_bins=(
  sample_cpp_request_reply_async_sample
  sample_cpp_pair_recv_sample
  sample_cpp_pubsub_recv_sample
  sample_cpp_dealer_router_recv_sample
  sample_cpp_stream_recv_sample
  sample_cpp_stream_packet_callback_sample
  sample_cpp_spot_recv_sample
  sample_cpp_spot_request_async_sample
  sample_cpp_monitor_recv_sample
  sample_cpp_discovery_registry_sample
  sample_cpp_registry_query_sample
  sample_cpp_actor_room_server_sample
  sample_cpp_actor_gateway_relay_sample
  sample_cpp_actor_single_player_queue_sample
)

pass_count=0

for sample_bin in "${sample_bins[@]}"; do
  echo "[sample] ${sample_bin}"
  "${BUILD_DIR}/${sample_bin}"
  pass_count=$((pass_count + 1))
done

echo "[cpp-samples] sample summary: ${pass_count}/${#sample_bins[@]} passed"
