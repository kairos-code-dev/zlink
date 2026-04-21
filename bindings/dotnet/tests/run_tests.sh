#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT="${ROOT_DIR}/tests/Zlink.Tests/Zlink.Tests.csproj"
CORE_LIB="${ROOT_DIR}/../core/build/lib/libzlink.so.5.3.2"

if [[ -f "${CORE_LIB}" ]]; then
    export ZLINK_LIBRARY_PATH="${CORE_LIB}"
    while IFS= read -r native_dir; do
        cp -f "${CORE_LIB}" "${native_dir}/libzlink.so.5.3.2"
        ln -sfn libzlink.so.5.3.2 "${native_dir}/libzlink.so.5"
        ln -sfn libzlink.so.5 "${native_dir}/libzlink.so"
    done < <(find "${ROOT_DIR}" -type d -path '*linux-x64/native')
fi

echo "[dotnet-tests] running ${PROJECT}"
if dotnet test "${PROJECT}" "$@"; then
    echo "[dotnet-tests] PASS"
else
    status=$?
    echo "[dotnet-tests] FAIL (${status})"
    exit "${status}"
fi
