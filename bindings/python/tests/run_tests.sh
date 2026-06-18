#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHONPATH_ENTRIES=("${ROOT_DIR}/src")
if [[ -n "${PYTHONPATH:-}" ]]; then
  PYTHONPATH_ENTRIES+=("${PYTHONPATH}")
fi
export PYTHONPATH="$(IFS=:; printf '%s' "${PYTHONPATH_ENTRIES[*]}")"
export PYTHONDONTWRITEBYTECODE="${PYTHONDONTWRITEBYTECODE:-1}"

cd "${ROOT_DIR}"
pytest -q "$@"
"${ROOT_DIR}/samples/run_samples.sh"
