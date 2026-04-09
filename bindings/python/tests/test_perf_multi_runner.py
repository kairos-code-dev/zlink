import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PERF_DIR = ROOT / "perf"


class PerfMultiRunnerTests(unittest.TestCase):
    def assert_no_python_perf_server_process(self):
        result = subprocess.run(
            [
                "ps",
                "-eo",
                "pid,cmd",
            ],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0)
        lines = [
            line
            for line in result.stdout.splitlines()
            if "bindings/python/perf/multi/perf_multi_" in line
            and "_server.py" in line
        ]
        self.assertEqual(lines, [], msg="\n".join(lines))

    def test_top_level_multi_wrapper_help(self):
        result = subprocess.run(
            [str(PERF_DIR / "run_benchmarks_multi.sh"), "--help"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("usage: run_benchmarks.sh", result.stdout)
        self.assertIn("--clients", result.stdout)
        self.assertIn("--msg-sizes", result.stdout)

    def test_multi_runner_help(self):
        result = subprocess.run(
            [str(PERF_DIR / "multi" / "run_benchmarks.sh"), "--help"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("usage: run_benchmarks.sh", result.stdout)
        self.assertIn("--clients", result.stdout)
        self.assertIn("--results-dir", result.stdout)

    def test_multi_pubsub_runner_executes(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "multi" / "run_benchmarks.sh"),
                "--pattern",
                "PUBSUB",
                "--duration",
                "0.2",
                "--msg-sizes",
                "64",
                "--clients",
                "2",
            ],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("RESULT,current,MULTI_PUBSUB,tcp,64,throughput,", result.stdout)
        self.assert_no_python_perf_server_process()

    def test_multi_spot_runner_executes(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "multi" / "run_benchmarks.sh"),
                "--pattern",
                "SPOT",
                "--duration",
                "0.2",
                "--msg-sizes",
                "64",
                "--clients",
                "2",
            ],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=20,
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("RESULT,current,MULTI_SPOT,tcp,64,throughput,", result.stdout)
        self.assert_no_python_perf_server_process()

    def test_multi_runner_writes_policy_style_report(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            result = subprocess.run(
                [
                    str(PERF_DIR / "run_benchmarks_multi.sh"),
                    "--pattern",
                    "PUBSUB",
                    "--duration",
                    "0.2",
                    "--msg-sizes",
                    "64",
                    "--clients",
                    "2",
                    "--results-dir",
                    tmpdir,
                    "--results-tag",
                    "smoke",
                ],
                cwd=str(ROOT),
                capture_output=True,
                text=True,
                timeout=25,
            )
            self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
            self.assertIn("## Effective Options (start)", result.stdout)
            self.assertIn("| Pattern | Transport | Size |", result.stdout)
            reports = list(Path(tmpdir).glob("perf_python_multi_linux_*_smoke.txt"))
            self.assertEqual(len(reports), 1)
            report_text = reports[0].read_text(encoding="utf-8")
            self.assertIn("RESULT,current,MULTI_PUBSUB,tcp,64,throughput,", report_text)
            self.assertRegex(
                reports[0].name,
                r"^perf_python_multi_linux_\d{8}_\d{6}_smoke\.txt$",
            )
            self.assert_no_python_perf_server_process()
