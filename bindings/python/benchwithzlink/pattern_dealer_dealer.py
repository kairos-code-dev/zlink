#!/usr/bin/env python3
import sys

from bench_common import parse_pattern_args, zlink
from pair_like import run_pair_like


def run(transport: str, size: int) -> int:
    return run_pair_like(
        "DEALER_DEALER",
        int(zlink.SocketType.DEALER),
        int(zlink.SocketType.DEALER),
        transport,
        size,
    )


def main_from_args(args) -> int:
    parsed = parse_pattern_args("DEALER_DEALER", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
