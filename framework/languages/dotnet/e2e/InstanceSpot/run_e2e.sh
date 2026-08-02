#!/usr/bin/env bash
set -euo pipefail
umask 077

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
scenario="${*:-all}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
CONFIG_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$CONFIG_DIR"
}
trap cleanup EXIT

python3 "$SCRIPT_DIR/../write_role_config.py" "$CONFIG_DIR/instance-spot.json" -- \
  --scenario "$scenario"

cat >&2 <<EOF
InstanceSpot '${scenario}' is not executable yet.
Config 14 has a feature-map inventory but no process fixture, role server, or
client evidence. The aggregate runner refuses to count this configuration as
passed until those artifacts exist.
EOF
exit 2
