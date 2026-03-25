import importlib.util
import os
import pathlib
import sys
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

    def test_single_result_dir_uses_report_path(self):
        root = os.path.join(os.sep, "tmp", "perf-root")
        self.assertEqual(
            RC.single_result_dir(root),
            os.path.join(root, "single", "report"),
        )

    def test_result_filename_includes_recv_mode(self):
        self.assertRegex(
            RC.build_result_filename("recv", "tag"),
            r"^perf_[a-z]+_recv_\d{8}_\d{6}_tag\.txt$",
        )
        self.assertRegex(
            RC.build_result_filename("callback"),
            r"^perf_[a-z]+_callback_\d{8}_\d{6}\.txt$",
        )

    def test_resolve_recv_mode_rejects_invalid_value(self):
        self.assertEqual(RC.resolve_recv_mode("recv"), "recv")
        self.assertEqual(RC.resolve_recv_mode("CALLBACK"), "callback")
        with self.assertRaises(ValueError):
            RC.resolve_recv_mode("bogus")

    def test_single_default_recv_mode_is_callback(self):
        self.assertEqual(RC.resolve_recv_mode(""), "callback")

    def test_recv_mode_uses_current_targets(self):
        self.assertEqual(RC.resolve_binary_name("PAIR", "callback"), "perf_pair")
        self.assertEqual(
            RC.resolve_binary_name("PUBSUB", "callback"), "perf_pubsub"
        )
        self.assertEqual(
            RC.resolve_binary_name("SPOT", "callback"), "perf_spot"
        )
        with self.assertRaises(ValueError):
            RC.resolve_binary_name("PAIR", "recv")
        with self.assertRaises(ValueError):
            RC.resolve_binary_name("SPOT", "recv")

    def test_collect_unsupported_patterns_matches_current_single_matrix(self):
        self.assertEqual(
            RC.collect_unsupported_patterns(
                ["PAIR", "SPOT"], "callback"
            ),
            [],
        )
        self.assertEqual(
            RC.collect_unsupported_patterns(["PAIR", "SPOT"], "recv"),
            ["PAIR", "SPOT"],
        )


if __name__ == "__main__":
    unittest.main()
