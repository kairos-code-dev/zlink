#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT="${ROOT_DIR}/tests/Zlink.Tests/Zlink.Tests.csproj"

echo "[dotnet-tests] running ${PROJECT}"
if dotnet test "${PROJECT}" "$@"; then
    echo "[dotnet-tests] PASS"
else
    status=$?
    echo "[dotnet-tests] FAIL (${status})"
    exit "${status}"
fi
