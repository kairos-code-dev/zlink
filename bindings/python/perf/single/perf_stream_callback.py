#!/usr/bin/env python3
import sys

import perf_stream
from perf_common import parse_pattern_args


def run(transport: str, size: int) -> int:
    return perf_stream.run_mode(
        transport,
        size,
        len32be_dispatch=False,
        pattern="STREAM_CALLBACK",
    )


def main_from_args(args) -> int:
    parsed = parse_pattern_args("STREAM_CALLBACK", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
