#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUTPUT="${1:-}"

if [[ -z "$OUTPUT" ]]; then
  echo "Usage: $0 <output-manifest>" >&2
  exit 2
fi

version_value() {
  sed -n 's/^LIBZLINK_VERSION=//p' "$REPO_DIR/VERSION"
}

dir_hash() {
  local root="$1"
  (
    cd "$root"
    find . -type f -print0 | sort -z | while IFS= read -r -d '' file; do
      printf '%s  %s\n' "$(sha256sum "$file" | awk '{print $1}')" "${file#./}"
    done
  ) | sha256sum | awk '{print $1}'
}

repo_hash() {
  (
    cd "$REPO_DIR"
    find "$@" -type f -print0 | sort -z | while IFS= read -r -d '' file; do
      printf '%s  %s\n' "$(sha256sum "$file" | awk '{print $1}')" "$file"
    done
  ) | sha256sum | awk '{print $1}'
}

macro_value() {
  local name="$1"
  rg -N "^#define ${name} " "$REPO_DIR/core/include" | awk '{print $3}' | sed 's/[uUlL]*$//' | sort -u | paste -sd, -
}

version="$(version_value)"
runtime="$(readlink -f "$REPO_DIR/core/build/lib/libzlink.so" 2>/dev/null || true)"
if [[ -z "$version" || -z "$runtime" || ! -f "$runtime" ]]; then
  echo "Core version or official runtime is missing." >&2
  exit 1
fi
if find "$REPO_DIR/core/include" "$REPO_DIR/core/src" -type f -newer "$runtime" -print -quit | grep -q .; then
  echo "Core runtime is older than core/include or core/src: $runtime" >&2
  exit 1
fi
if [[ "$(basename "$runtime")" != "libzlink.so.$version" ]]; then
  echo "Core runtime filename does not match VERSION: $(basename "$runtime") != libzlink.so.$version" >&2
  exit 1
fi

runtime_version="$(python3 - "$runtime" <<'PY'
import ctypes
import sys
lib = ctypes.CDLL(sys.argv[1])
major = ctypes.c_int()
minor = ctypes.c_int()
patch = ctypes.c_int()
lib.zlink_version(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
print(f"{major.value}.{minor.value}.{patch.value}")
PY
)"
[[ "$runtime_version" == "$version" ]] || {
  echo "Runtime zlink_version does not match VERSION: $runtime_version != $version" >&2
  exit 1
}

layout_source="$(mktemp --suffix=.c)"
layout_bin="$(mktemp)"
tmp="$(mktemp)"
trap 'rm -f "$tmp" "$layout_source" "$layout_bin"' EXIT
cat >"$layout_source" <<'EOF'
#include <stddef.h>
#include <stdio.h>
#include <stdalign.h>
#include <zlink.h>
#define SHOW(T) printf(#T "=%zu/%zu\n", sizeof(T), alignof(T))
int main(void) {
  SHOW(zlink_mesh_node_options_t); SHOW(zlink_mesh_peer_connection_options_t);
  SHOW(zlink_mesh_node_status_t); SHOW(zlink_mesh_peer_entry_t);
  SHOW(zlink_mesh_ready_record_t); SHOW(zlink_mesh_claim_t);
  SHOW(zlink_mesh_receive_record_t); SHOW(zlink_mesh_reply_token_t);
  SHOW(zlink_spot_status_t); SHOW(zlink_actor_ref_t); SHOW(zlink_actor_location_t);
  SHOW(zlink_actor_transfer_prepare_t); SHOW(zlink_actor_transfer_prepare_result_t);
  SHOW(zlink_actor_transfer_token_t); SHOW(zlink_stream_session_binding_t);
  SHOW(zlink_stream_session_status_t); return 0;
}
EOF
cc -std=c11 -I"$REPO_DIR/core/include" "$layout_source" -o "$layout_bin"
layout_values="$($layout_bin | sort | paste -sd';' -)"

cat >"$tmp" <<EOF
FORMAT=1
CORE_VERSION=$version
CORE_REVISION=$(git -C "$REPO_DIR" rev-parse HEAD)
CORE_SPEC_SHA256=$(dir_hash "$REPO_DIR/core/doc/spec/core")
CORE_HEADER_SHA256=$(dir_hash "$REPO_DIR/core/include")
CORE_SOURCE_SHA256=$(repo_hash core/src core/include core/doc/spec/core)
CORE_RUNTIME_PATH=${runtime#"$REPO_DIR/"}
CORE_RUNTIME_SHA256=$(sha256sum "$runtime" | awk '{print $1}')
CORE_RUNTIME_VERSION=$runtime_version
CORE_SYMBOL_SHA256=$(nm -D --defined-only "$runtime" | awk '{print $3}' | sed -n '/^zlink_/p' | sort -u | sha256sum | awk '{print $1}')
CORE_SONAME=$(readelf -d "$runtime" | sed -n 's/.*Library soname: \[\(.*\)\].*/\1/p')
CORE_SERVICE_ABI=mesh_node:$(macro_value ZLINK_MESH_NODE_ABI_VERSION),dispatch:$(macro_value ZLINK_MESH_DISPATCH_ABI_VERSION),spot:$(macro_value ZLINK_SPOT_ABI_VERSION),actor:$(macro_value ZLINK_ACTOR_ABI_VERSION),stream_session:$(macro_value ZLINK_STREAM_SESSION_ABI_VERSION),monitor:$(macro_value ZLINK_MESH_MONITOR_ABI_VERSION)
CORE_LAYOUTS=$layout_values
EOF
install -m 0644 "$tmp" "$OUTPUT"
echo "Candidate manifest: $OUTPUT"
