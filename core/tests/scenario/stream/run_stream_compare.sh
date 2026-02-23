#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

RUNNER="${ROOT_DIR}/core/bench/with_stream/run_benchmarks.sh"
if [[ ! -x "${RUNNER}" ]]; then
  RUNNER="${ROOT_DIR}/core/bench/benchwithstreamcompare/run_benchmarks.sh"
fi

exec "${RUNNER}" "$@"
