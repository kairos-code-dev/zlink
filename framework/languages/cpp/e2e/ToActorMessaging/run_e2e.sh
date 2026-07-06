#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${ZLINK_CPP_BUILD_DIR:-"$SCRIPT_DIR/../../build"}
E2E_START_ORDER="${E2E_START_ORDER:-forward}"

ordered_roles() {
  python3 - "$E2E_START_ORDER" "$@" <<'PY'
import random
import sys

mode = sys.argv[1]
roles = sys.argv[2:]
if mode in ("", "forward"):
    pass
elif mode == "reverse":
    roles.reverse()
elif mode.startswith("shuffle:"):
    seed_text = mode.split(":", 1)[1]
    if seed_text == "":
        raise SystemExit("E2E_START_ORDER shuffle requires a seed")
    random.Random(int(seed_text)).shuffle(roles)
else:
    raise SystemExit(f"unsupported E2E_START_ORDER={mode!r}")
for role in roles:
    print(role)
PY
}

start_role() {
  case "$1" in
    actor) "$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_actor" ;;
    caller) "$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_caller" ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

echo "start_order=$E2E_START_ORDER"
mapfile -t SERVER_ROLES < <(ordered_roles actor caller)
for role in "${SERVER_ROLES[@]}"; do
  start_role "$role"
done

"$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_client"
