import importlib.util
import json
import os
import pathlib
import sys
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "run_comparison.py"
SPEC = importlib.util.spec_from_file_location("single_run_comparison", MODULE_PATH)
RC = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
sys.modules[SPEC.name] = RC
SPEC.loader.exec_module(RC)


class EnvPatch:
    def __init__(self, updates=None, remove=None):
        self.updates = updates or {}
        self.remove = remove or []
        self._backup = {}

    def __enter__(self):
        keys = set(self.updates.keys()) | set(self.remove)
        for key in keys:
            self._backup[key] = os.environ.get(key)

        for key in self.remove:
            os.environ.pop(key, None)
        for key, value in self.updates.items():
            os.environ[key] = value
        return self

    def __exit__(self, exc_type, exc, tb):
        for key, value in self._backup.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


class RunComparisonPolicyTests(unittest.TestCase):
    def test_direction_and_throughput_unit_are_one_way(self):
        self.assertEqual(RC.pattern_direction_label("PAIR"), "one-way")
        self.assertTrue(RC.format_throughput("PAIR", 1234.0).endswith("Kmsg/s"))

    def test_default_message_sizes_follow_policy_matrix(self):
        self.assertEqual(
            RC.default_msg_sizes_for_pattern("PAIR"),
            [64, 256, 1024, 65536, 131072, 262144],
        )
        self.assertEqual(
            RC.default_msg_sizes_for_pattern("GATEWAY"),
            [64, 256, 1024, 65536, 131072, 262144],
        )
        self.assertEqual(
            RC.default_msg_sizes_for_pattern("SPOT"),
            [64, 256, 1024, 65536, 131072, 262144],
        )

    def test_default_transports_follow_policy_matrix(self):
        with EnvPatch(remove=["PERF_TRANSPORTS"]):
            pair_transports = RC.select_transports("PAIR")
            stream_transports = RC.select_transports("SPOT")

        self.assertEqual(pair_transports[:5], ["tcp", "tls", "ws", "wss", "inproc"])
        if RC.IS_WINDOWS:
            self.assertNotIn("ipc", pair_transports)
        else:
            self.assertIn("ipc", pair_transports)
        self.assertEqual(stream_transports, ["tcp", "tls", "ws", "wss"])

    def test_stream_patterns_removed_from_single_runner(self):
        with self.assertRaises(ValueError):
            RC.parse_pattern_arg("STREAM")

    def test_threshold_rules_reject_bandwidth_metric(self):
        rules_data = {
            "PAIR/tcp/bandwidth": {"warning": -10, "fail": -15},
            "PAIR/tcp/throughput": {"warning": -10, "fail": -15},
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            json.dump(rules_data, fh)
            path = fh.name
        try:
            with EnvPatch(
                updates={"PERF_THRESHOLDS_FILE": path},
            ):
                rules, warnings = RC.load_threshold_rules()
        finally:
            os.remove(path)

        self.assertEqual(len(rules), 1)
        self.assertEqual(rules[0].metric, "throughput")
        self.assertTrue(any("PAIR/tcp/bandwidth" in msg for msg in warnings))

    def test_baseline_compare_ignores_bandwidth_metric(self):
        key = ("current", "PAIR", "tcp", 64, "bandwidth")
        warnings, fails, skipped = RC.compare_against_baseline(
            "trend",
            {key: 100.0},
            {},
            [],
        )
        self.assertEqual(warnings, [])
        self.assertEqual(fails, [])
        self.assertEqual(skipped, 0)

    def test_baseline_compare_warns_on_missing_throughput_baseline(self):
        key = ("current", "PAIR", "tcp", 64, "throughput")
        warnings, fails, skipped = RC.compare_against_baseline(
            "trend",
            {key: 100.0},
            {},
            [],
        )
        self.assertEqual(fails, [])
        self.assertEqual(skipped, 1)
        self.assertTrue(any("baseline missing" in msg for msg in warnings))


if __name__ == "__main__":
    unittest.main()
