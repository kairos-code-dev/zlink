#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

_THIS = Path(__file__).resolve().parent
if str(_THIS) not in sys.path:
    sys.path.insert(0, str(_THIS))

from perf_multi_main import main_from_args  # type: ignore


PATTERN = "MULTI_STREAM"


def main() -> int:
    if len(sys.argv) < 3:
        return 1
    transport = sys.argv[1]
    size = sys.argv[2]
    rest = sys.argv[3:]
    argv = [
        "--role",
        "client",
        "--pattern",
        PATTERN,
        "--transport",
        transport,
        "--size",
        size,
    ] + rest
    return main_from_args(argv)


if __name__ == "__main__":
    raise SystemExit(main())
