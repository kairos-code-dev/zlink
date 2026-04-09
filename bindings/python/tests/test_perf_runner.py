import os
import re
import subprocess
import sys
import tempfile
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
        self.assertIn("--msg-sizes", result.stdout)
        self.assertIn("--results-dir", result.stdout)

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

    def test_single_runner_writes_policy_style_report(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            result = subprocess.run(
                [
                    str(PERF_DIR / "run_benchmarks.sh"),
                    "--pattern",
                    "PAIR",
                    "--msg-sizes",
                    "64",
                    "--warmup",
                    "0.1",
                    "--duration",
                    "0.2",
                    "--results-dir",
                    tmpdir,
                    "--results-tag",
                    "smoke",
                ],
                cwd=str(ROOT),
                capture_output=True,
                text=True,
                timeout=20,
            )
            self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
            self.assertIn("## Effective Options (start)", result.stdout)
            self.assertIn("| Pattern | Transport | Size |", result.stdout)
            reports = list(Path(tmpdir).glob("perf_python_single_linux_*_smoke.txt"))
            self.assertEqual(len(reports), 1)
            report_text = reports[0].read_text(encoding="utf-8")
            self.assertIn("RESULT,current,PAIR,tcp,64,throughput,", report_text)
            self.assertRegex(
                reports[0].name,
                r"^perf_python_single_linux_\d{8}_\d{6}_smoke\.txt$",
            )
