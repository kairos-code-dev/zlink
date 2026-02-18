#!/usr/bin/env bash
set -euo pipefail

RESULT_DIR="doc/plan/baseline/20260217_phase0_zlink1024" \
./core/tests/scenario/stream/run_stream_compare.sh \
  --stack zlink \
  --size 1024 \
  --ccu 1000 \
  --duration 5 \
  --repeats 3 \
  --inflight 1 \
  --client-io-threads 1 \
  --server-io-threads 1
