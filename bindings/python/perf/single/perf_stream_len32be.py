#!/usr/bin/env python3
import sys

from perf_common import parse_pattern_args
import perf_stream


def main_from_args(args) -> int:
    parsed = parse_pattern_args("STREAM_LEN32BE", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return perf_stream.run_len32be(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
