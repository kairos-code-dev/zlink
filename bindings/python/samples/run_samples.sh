#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "${ROOT_DIR}/../.." && pwd)"
export ZLINK_LIBRARY_PATH="${ZLINK_LIBRARY_PATH:-${REPO_DIR}/core/build/lib/libzlink.so}"
export PYTHONPATH="${ROOT_DIR}/src:${ROOT_DIR}/samples${PYTHONPATH:+:${PYTHONPATH}}"

cd "${ROOT_DIR}"
python3 samples/run_samples.py "$@"
