#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
CANONICAL_SCRIPT="${ROOT_DIR}/bindings/cpp/plan/bindings/run_cpp_bindings_alignment_execution.sh"

if [[ ! -x "${CANONICAL_SCRIPT}" ]]; then
  echo "Canonical execution script not found: ${CANONICAL_SCRIPT}" >&2
  exit 1
fi

exec "${CANONICAL_SCRIPT}" "$@"
