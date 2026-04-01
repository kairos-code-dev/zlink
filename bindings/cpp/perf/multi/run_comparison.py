#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


def main() -> int:
    runner = Path(__file__).resolve().parents[1] / "run_policy_bench.py"
    cmd = [sys.executable, str(runner), "--binding", "cpp", "--suite", "multi"]
    cmd.extend(sys.argv[1:])
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
