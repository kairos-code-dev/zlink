#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT="${ROOT_DIR}/tests/Zlink.Tests/Zlink.Tests.csproj"
VERSION_FILE="${ROOT_DIR}/../../VERSION"
CORE_LIB_DIR="${ROOT_DIR}/../../core/build/lib"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so.${CORE_VERSION}"

sync_native_dirs() {
    local search_root="$1"
    [[ -d "${search_root}" ]] || return 0

    while IFS= read -r native_dir; do
        rm -f "${native_dir}/libzlink.so" \
            "${native_dir}/libzlink.so.5" \
            "${native_dir}/libzlink.so."*
        cp -f "${CORE_LIB}" "${native_dir}/libzlink.so.${CORE_VERSION}"
        ln -sfn "libzlink.so.${CORE_VERSION}" "${native_dir}/libzlink.so.5"
        ln -sfn libzlink.so.5 "${native_dir}/libzlink.so"
    done < <(find "${search_root}" -type d -path '*linux-x64/native')
}

if [[ -f "${CORE_LIB}" ]]; then
    export ZLINK_LIBRARY_PATH="${CORE_LIB}"
fi

echo "[dotnet-tests] building ${PROJECT}"
dotnet build "${PROJECT}"

if [[ -f "${CORE_LIB}" ]]; then
    sync_native_dirs "${ROOT_DIR}/tests/Zlink.Tests/bin"
fi

echo "[dotnet-tests] running ${PROJECT}"
if dotnet test "${PROJECT}" --no-build "$@"; then
    echo "[dotnet-tests] PASS"
else
    status=$?
    echo "[dotnet-tests] FAIL (${status})"
    exit "${status}"
fi
