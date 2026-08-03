#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${ZLINK_LIBRARY_PATH:-}" ]]; then
    echo "Set ZLINK_LIBRARY_PATH to an approved Core candidate runtime." >&2
    exit 2
fi
export PYTHONPATH="${ROOT_DIR}/src:${ROOT_DIR}/samples${PYTHONPATH:+:${PYTHONPATH}}"

cd "${ROOT_DIR}"
python3 samples/run_samples.py "$@"
