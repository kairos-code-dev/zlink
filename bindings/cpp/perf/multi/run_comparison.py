#!/usr/bin/env python3
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    runner = Path(__file__).resolve().parents[1] / "run_comparison.py"
    cmd = [sys.executable, str(runner)]
    cmd.extend(sys.argv[1:])
    env = dict(os.environ)
    env["PERF_INTERNAL_MULTI_ENTRY"] = "1"
    return subprocess.call(cmd, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
