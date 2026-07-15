#!/usr/bin/env bash

zlink_e2e_start_order_mode() {
  printf '%s\n' "${E2E_START_ORDER:-forward}"
}

zlink_e2e_order_roles() {
  local mode
  mode="$(zlink_e2e_start_order_mode)"

  python3 - "${mode}" "$@" <<'PY'
import random
import sys

mode = sys.argv[1]
roles = sys.argv[2:]

if mode == "forward":
    pass
elif mode == "reverse":
    roles.reverse()
elif mode.startswith("shuffle:"):
    seed_text = mode.split(":", 1)[1]
    if not seed_text:
        raise SystemExit("E2E_START_ORDER shuffle requires a seed")
    try:
        seed = int(seed_text)
    except ValueError as error:
        raise SystemExit("E2E_START_ORDER shuffle seed must be an integer") from error
    random.Random(seed).shuffle(roles)
else:
    raise SystemExit(f"unsupported E2E_START_ORDER={mode!r}")

print("\n".join(roles))
PY
}
