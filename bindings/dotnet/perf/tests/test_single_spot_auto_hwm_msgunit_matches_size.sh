#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_PERF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNNER="${DOTNET_PERF_DIR}/run_benchmarks.sh"

sizes="${PERF_DOTNET_SINGLE_SPOT_MSGUNIT_SIZES:-64,256,1024}"
duration="${PERF_DOTNET_SINGLE_SPOT_MSGUNIT_DURATION:-1}"
tag="${PERF_DOTNET_SINGLE_SPOT_MSGUNIT_TAG:-single_spot_auto_hwm_msgunit_matches_size}"
log_file="$(mktemp -t zlink-dotnet-single-spot-msgunit.XXXXXX.log)"
trap 'rm -f "${log_file}"' EXIT

set +e
PERF_FAIL_FAST=1 \
"${RUNNER}" \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes "${sizes}" \
  --duration "${duration}" \
  --results-tag "${tag}" \
  >"${log_file}" 2>&1
status=$?
set -e

if [[ "${status}" -ne 0 ]]; then
  cat "${log_file}" >&2
  exit "${status}"
fi

python3 - "${log_file}" <<'PY'
import sys

log_path = sys.argv[1]
in_spot = False
checked = 0
bad = []

with open(log_path, encoding="utf-8", errors="replace") as f:
    for raw in f:
        line = raw.strip()
        if line == "- pattern: SPOT":
            in_spot = True
            continue
        if in_spot and line.startswith("## ") and line != "## Auto-HWM Detail":
            in_spot = False
        if not in_spot or not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if not cells or cells[0] in {"Size(B)", "---------"}:
            continue
        if len(cells) < 11:
            continue
        size = cells[0]
        msg_unit = cells[10]
        checked += 1
        if size != msg_unit:
            bad.append((size, msg_unit, line))

if checked == 0:
    print("no SPOT Auto-HWM rows found", file=sys.stderr)
    sys.exit(1)
if bad:
    for size, msg_unit, line in bad:
        print(
            f"SPOT Auto-HWM MsgUnit(B) mismatch: Size(B)={size}, "
            f"MsgUnit(B)={msg_unit}: {line}",
            file=sys.stderr,
        )
    sys.exit(1)
PY

if ! rg -q "status: complete" "${log_file}"; then
  cat "${log_file}" >&2
  exit 1
fi
