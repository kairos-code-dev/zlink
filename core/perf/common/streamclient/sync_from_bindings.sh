#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

SRC_DIR="${ROOT_DIR}/bindings/bench/stream_client"
DST_CLIENT="${ROOT_DIR}/core/perf/common/streamclient/perf_stream_client.cpp"

cp "${SRC_DIR}/bench_stream_client.cpp" "${DST_CLIENT}"

echo "synced stream client from bindings -> core/perf/common/streamclient/perf_stream_client.cpp"
