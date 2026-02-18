#!/usr/bin/env bash
set -euo pipefail
RESULT_DIR="doc/plan/baseline/20260218_profile_hotspot_z64_iterA/benchmark" \
  /home/hep7/project/kairos/zlink-perf-wt/core/tests/scenario/stream/run_stream_compare.sh --stack zlink --size 64 --ccu 10000 --duration 5 --repeats 1 --inflight 1 --client-io-threads 4 --server-io-threads 8 
