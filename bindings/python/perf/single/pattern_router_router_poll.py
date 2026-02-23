#!/usr/bin/env python3
import sys

from bench_common import parse_pattern_args
from pattern_router_router import run_router_router


def run(transport: str, size: int) -> int:
    return run_router_router(transport, size, True)


def main_from_args(args) -> int:
    parsed = parse_pattern_args("ROUTER_ROUTER_POLL", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
