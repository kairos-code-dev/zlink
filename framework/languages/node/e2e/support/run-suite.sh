#!/usr/bin/env bash
set -euo pipefail

SUITE_NAME="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${NODE_ROOT}"
node scripts/ensure_node_binding_dist.js
npm run build >/dev/null
node "${SCRIPT_DIR}/node-e2e-runner.js" "${SUITE_NAME}"
