#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[3]
runner = ROOT / "bindings" / "java" / "perf" / "run_comparison.py"

rc = subprocess.call([sys.executable, str(runner)] + sys.argv[1:],
                     env=os.environ.copy())
raise SystemExit(rc)
