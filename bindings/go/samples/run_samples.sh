#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

SAMPLES=(
  "samples/request_reply_async_sample"
  "samples/request_reply_callback_sample"
  "samples/pair_recv_sample"
  "samples/pair_callback_sample"
  "samples/pubsub_recv_sample"
  "samples/pubsub_callback_sample"
  "samples/dealer_router_recv_sample"
  "samples/dealer_router_callback_sample"
  "samples/stream_recv_sample"
  "samples/stream_callback_sample"
  "samples/spot_recv_sample"
  "samples/spot_callback_sample"
  "samples/monitor_recv_sample"
  "samples/discovery_registry_sample"
  "samples/registry_query_sample"
)

pass=0
fail=0

for sample in "${SAMPLES[@]}"; do
  echo "==> ${sample}"
  if go run "./${sample}"; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
  fi
  sleep 1
done

echo "samples: pass=${pass} fail=${fail}"
if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi
