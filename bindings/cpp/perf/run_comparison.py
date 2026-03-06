#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="C++ perf wrapper")
    parser.add_argument("--suite", choices=("single", "multi"), default="single")
    args, rest = parser.parse_known_args()

    root = Path(__file__).resolve().parents[2]
    runner = root / "bindings" / "perf" / "run_policy_bench.py"
    cmd = [sys.executable, str(runner), "--binding", "cpp", "--suite", args.suite]
    cmd.extend(rest)
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
