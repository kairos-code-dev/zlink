#!/usr/bin/env bash
set -euo pipefail

scenario="${1:-all}"
echo "[node-e2e] ChannelEgressRouting scenario=${scenario} BLOCKED: Config 12 role servers are not implemented for Node." >&2
exit 2
