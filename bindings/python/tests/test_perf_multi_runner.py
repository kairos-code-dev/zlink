import os
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PERF_DIR = ROOT / "perf"


class PerfMultiRunnerTests(unittest.TestCase):
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

    def test_multi_pubsub_runner_executes(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "multi" / "run_benchmarks.sh"),
                "--pattern",
                "PUBSUB",
                "--duration",
                "0.2",
                "--warmup",
                "0.1",
                "--msg-size",
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

    def test_multi_dealer_router_runner_executes(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "multi" / "run_benchmarks.sh"),
                "--pattern",
                "DEALER_ROUTER",
                "--duration",
                "0.2",
                "--warmup",
                "0.1",
                "--msg-size",
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
        self.assertIn(
            "RESULT,current,MULTI_DEALER_ROUTER,tcp,64,throughput,",
            result.stdout,
        )

    def test_multi_router_router_runner_executes(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "multi" / "run_benchmarks.sh"),
                "--pattern",
                "ROUTER_ROUTER",
                "--duration",
                "0.2",
                "--warmup",
                "0.1",
                "--msg-size",
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
        self.assertIn(
            "RESULT,current,MULTI_ROUTER_ROUTER,tcp,64,throughput,",
            result.stdout,
        )

    def test_multi_spot_runner_executes(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "multi" / "run_benchmarks.sh"),
                "--pattern",
                "SPOT",
                "--duration",
                "0.2",
                "--warmup",
                "0.1",
                "--msg-size",
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

    def test_multi_runner_accepts_comma_separated_pattern_list(self):
        result = subprocess.run(
            [
                str(PERF_DIR / "run_benchmarks_multi.sh"),
                "--pattern",
                "ROUTER_ROUTER,SPOT",
                "--duration",
                "0.2",
                "--warmup",
                "0.1",
                "--msg-size",
                "64",
                "--clients",
                "2",
            ],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn(
            "RESULT,current,MULTI_ROUTER_ROUTER,tcp,64,throughput,",
            result.stdout,
        )
        self.assertIn("RESULT,current,MULTI_SPOT,tcp,64,throughput,", result.stdout)
