#!/usr/bin/env bash
set -euo pipefail

scenario="${1:-all}"
echo "[node-e2e] InstanceSpot scenario=${scenario} BLOCKED: Config 14 role servers are not implemented for Node." >&2
exit 2
