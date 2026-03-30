import os
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PERF_DIR = ROOT / "perf"


class PerfRunnerTests(unittest.TestCase):
    def test_single_runner_help(self):
        result = subprocess.run(
            [str(PERF_DIR / "single" / "run_benchmarks.sh"), "--help"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("usage: run_benchmarks.sh", result.stdout)

    def test_pair_perf_entry_executes(self):
        result = subprocess.run(
            [
                sys.executable,
                str(PERF_DIR / "single" / "perf_pair.py"),
                "--duration",
                "0.2",
                "--warmup",
                "0.1",
                "--msg-size",
                "64",
            ],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=15,
            env={
                **os.environ,
                "PYTHONPATH": str(ROOT / "src"),
            },
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("RESULT,current,PAIR,tcp,64,throughput,", result.stdout)
