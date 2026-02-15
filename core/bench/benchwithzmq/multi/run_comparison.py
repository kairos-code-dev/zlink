#!/usr/bin/env python3
import os
import subprocess
import sys

MULTI_DEFAULT_PATTERNS = (
    "MULTI_DEALER_DEALER,"
    "MULTI_DEALER_ROUTER,"
    "MULTI_ROUTER_ROUTER,"
    "MULTI_ROUTER_ROUTER_POLL,"
    "MULTI_PUBSUB"
)

OPTIONS_WITHOUT_VALUE = {
    "--refresh-libzmq",
    "--zlink-only",
    "--pin-cpu",
    "-h",
    "--help",
}
OPTIONS_WITH_VALUE = {
    "--runs",
    "--build-dir",
}


def _error(message):
    print(f"Error: {message}", file=sys.stderr)
    return 1


def _normalize_pattern(raw_pattern):
    if not raw_pattern:
        return None
    if raw_pattern.upper() == "ALL":
        return MULTI_DEFAULT_PATTERNS

    parts = []
    for item in raw_pattern.split(","):
        p = item.strip()
        if not p:
            continue
        up = p.upper()
        if not up.startswith("MULTI_"):
            return None
        parts.append(up)

    if not parts:
        return None
    return ",".join(parts)


def _usage():
    print(
        """Usage: run_comparison_multi.py [MULTI_PATTERN] [options]

Runs only multi-socket benchmark patterns.
Default pattern is:
  MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,
  MULTI_ROUTER_ROUTER_POLL,MULTI_PUBSUB

Pattern must start with MULTI_* unless ALL is passed.

Example:
  run_comparison_multi.py MULTI_DEALER_DEALER
  run_comparison_multi.py ALL --runs 1 --build-dir /path/to/build
"""
    )


def _forward_args(argv):
    pattern = None
    forwarded = []
    i = 0
    n = len(argv)

    while i < n:
        arg = argv[i]
        if arg in ("-h", "--help"):
            _usage()
            raise SystemExit(0)
        if arg.startswith("-"):
            forwarded.append(arg)
            if arg in OPTIONS_WITH_VALUE:
                if i + 1 >= n:
                    raise SystemExit(_error(f"{arg} requires a value."))
                i += 1
                forwarded.append(argv[i])
            i += 1
            continue

        if pattern is None:
            pattern = _normalize_pattern(arg)
            if pattern is None:
                raise SystemExit(
                    _error(
                        "pattern must be MULTI_* or ALL for multi mode; "
                        "example: MULTI_DEALER_DEALER"
                    )
                )
            i += 1
            continue

        raise SystemExit(_error(f"Unexpected positional argument: {arg}"))

    if pattern is None:
        pattern = MULTI_DEFAULT_PATTERNS
    forwarded.insert(0, pattern)
    return forwarded


def main():
    base_script = os.path.join(
        os.path.dirname(os.path.dirname(__file__)),
        "run_comparison.py"
    )
    args = _forward_args(sys.argv[1:])
    return subprocess.call([sys.executable, base_script] + args)


if __name__ == "__main__":
    raise SystemExit(main())
