#!/usr/bin/env bash
set -euo pipefail
RESULT_DIR="/home/hep7/project/kairos/zlink-perf-wt/doc/plan/baseline/20260218_phase05_profile_zlink1024/benchmark" \
  /home/hep7/project/kairos/zlink-perf-wt/core/tests/scenario/stream/run_stream_compare.sh --stack zlink --size 1024 --ccu 10000 --duration 5 --repeats 1 --inflight 1 --client-io-threads 4 --server-io-threads 2 
