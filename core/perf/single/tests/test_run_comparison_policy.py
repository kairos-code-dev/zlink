import contextlib
import importlib.util
import builtins
import io
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
    def test_single_required_result_metrics_are_tier1_only(self):
        self.assertEqual(
            RC.REQUIRED_RESULT_METRICS,
            (
                "throughput",
                "bandwidth",
                "latency",
                RC.LATENCY_P95_METRIC,
                RC.LATENCY_P99_METRIC,
            ),
        )
        self.assertEqual(RC.REQUIRED_RESULT_METRIC_COUNT, 5)

    def test_single_collect_data_omits_legacy_queue_metrics(self):
        old_run_single_test = RC.run_single_test
        old_msg_sizes_for_pattern = RC.msg_sizes_for_pattern
        old_select_transports = RC.select_transports
        old_parse_args = RC.parse_args
        old_collect_missing_patterns = RC.collect_missing_patterns
        old_open = builtins.open
        old_makedirs = RC.os.makedirs
        old_stdout = RC.sys.stdout
        old_stderr = RC.sys.stderr
        old_build_result_filename = RC.build_result_filename
        old_single_result_dir = RC.single_result_dir
        old_print_effective_options = RC.print_effective_options
        old_env = os.environ.copy()

        class DummyFile:
            def write(self, _data):
                return 0

            def flush(self):
                return None

            def close(self):
                return None

        try:
            args = type(
                "Args",
                (),
                {
                    "pattern": "PAIR",
                    "runs": 1,
                    "duration": None,
                    "build_dir": "",
                    "pin_cpu": False,
                    "results_dir": "/tmp/ignored",
                    "results_tag": "",
                    "result_file": "",
                    "recv": "callback",
                },
            )()

            def fake_run_single_test(
                build_dir,
                current_lib_dir,
                binary_name,
                lib_name,
                pattern,
                transport,
                size,
                timeout_sec,
                pin_cpu,
            ):
                return RC.RunOutcome(
                    status="success",
                    throughput=1.0,
                    bandwidth=1.0,
                    latency=1.0,
                    latency_p95=1.0,
                    latency_p99=1.0,
                )

            RC.run_single_test = fake_run_single_test
            RC.msg_sizes_for_pattern = lambda pattern: [64]
            RC.select_transports = lambda pattern: ["tcp"]
            RC.parse_args = lambda: args
            RC.collect_missing_patterns = lambda *args, **kwargs: []
            builtins.open = lambda *args, **kwargs: DummyFile()
            RC.os.makedirs = lambda *args, **kwargs: None
            RC.build_result_filename = lambda mode, tag="": "dummy.txt"
            RC.single_result_dir = lambda root: root
            RC.print_effective_options = lambda *args, **kwargs: None

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                rc = RC.main()

            output = stdout.getvalue()
            self.assertEqual(rc, 0)
            self.assertIn("RESULT,current,PAIR,tcp,64,throughput,1.000", output)
            self.assertIn("RESULT,current,PAIR,tcp,64,latency_p95,1.000", output)
            self.assertIn("RESULT,current,PAIR,tcp,64,latency_p99,1.000", output)
            self.assertNotIn("Q.Snd.Max", output)
            self.assertNotIn("Q.Rcv.Max", output)
            self.assertNotIn("Q.Rcv.End", output)
        finally:
            RC.run_single_test = old_run_single_test
            RC.msg_sizes_for_pattern = old_msg_sizes_for_pattern
            RC.select_transports = old_select_transports
            RC.parse_args = old_parse_args
            RC.collect_missing_patterns = old_collect_missing_patterns
            builtins.open = old_open
            RC.os.makedirs = old_makedirs
            RC.sys.stdout = old_stdout
            RC.sys.stderr = old_stderr
            RC.build_result_filename = old_build_result_filename
            RC.single_result_dir = old_single_result_dir
            RC.print_effective_options = old_print_effective_options
            os.environ.clear()
            os.environ.update(old_env)

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

    def test_result_filename_uses_current_mode_label(self):
        self.assertRegex(
            RC.build_result_filename("tag"),
            r"^perf_[a-z]+_callback_\d{8}_\d{6}_tag\.txt$",
        )
        self.assertRegex(
            RC.build_result_filename(),
            r"^perf_[a-z]+_callback_\d{8}_\d{6}\.txt$",
        )

    def test_callback_mode_uses_current_targets(self):
        self.assertEqual(RC.resolve_binary_name("PAIR"), "perf_pair")
        self.assertEqual(RC.resolve_binary_name("PUBSUB"), "perf_pubsub")
        self.assertEqual(RC.resolve_binary_name("SPOT"), "perf_spot")

    def test_single_runner_executes_each_size_case_separately(self):
        old_run_single_test = RC.run_single_test
        old_msg_sizes_for_pattern = RC.msg_sizes_for_pattern
        old_select_transports = RC.select_transports
        old_parse_args = RC.parse_args
        old_collect_missing_patterns = RC.collect_missing_patterns
        old_open = builtins.open
        old_makedirs = RC.os.makedirs
        old_stdout = RC.sys.stdout
        old_stderr = RC.sys.stderr
        old_build_result_filename = RC.build_result_filename
        old_single_result_dir = RC.single_result_dir
        old_print_effective_options = RC.print_effective_options
        calls = []

        class DummyFile:
            def write(self, _data):
                return 0

            def flush(self):
                return None

            def close(self):
                return None

        try:
            args = type(
                "Args",
                (),
                {
                    "pattern": "PAIR",
                    "runs": 1,
                    "duration": None,
                    "build_dir": "",
                    "pin_cpu": False,
                    "results_dir": "/tmp/ignored",
                    "results_tag": "",
                    "result_file": "",
                    "recv": "callback",
                },
            )()
            RC.run_single_test = lambda build_dir, current_lib_dir, binary_name, lib_name, pattern, transport, size, timeout_sec, pin_cpu: (
                calls.append((binary_name, pattern, transport, size))
                or RC.RunOutcome(
                    status="success",
                    throughput=1.0,
                    bandwidth=1.0,
                    latency=1.0,
                    latency_p95=1.0,
                    latency_p99=1.0,
                )
            )
            RC.msg_sizes_for_pattern = lambda pattern: [64, 256]
            RC.select_transports = lambda pattern: ["tcp"]
            RC.parse_args = lambda: args
            RC.collect_missing_patterns = lambda *args, **kwargs: []
            builtins.open = lambda *args, **kwargs: DummyFile()
            RC.os.makedirs = lambda *args, **kwargs: None
            RC.build_result_filename = lambda mode, tag="": "dummy.txt"
            RC.single_result_dir = lambda root: root
            RC.print_effective_options = lambda *args, **kwargs: None

            rc = RC.main()

            self.assertEqual(rc, 0)
            self.assertEqual(
                calls,
                [
                    ("perf_pair", "PAIR", "tcp", 64),
                    ("perf_pair", "PAIR", "tcp", 256),
                ],
            )
        finally:
            RC.run_single_test = old_run_single_test
            RC.msg_sizes_for_pattern = old_msg_sizes_for_pattern
            RC.select_transports = old_select_transports
            RC.parse_args = old_parse_args
            RC.collect_missing_patterns = old_collect_missing_patterns
            builtins.open = old_open
            RC.os.makedirs = old_makedirs
            RC.sys.stdout = old_stdout
            RC.sys.stderr = old_stderr
            RC.build_result_filename = old_build_result_filename
            RC.single_result_dir = old_single_result_dir
            RC.print_effective_options = old_print_effective_options

    def test_single_transport_cooldown_runs_only_between_transports(self):
        old_run_single_test = RC.run_single_test
        old_msg_sizes_for_pattern = RC.msg_sizes_for_pattern
        old_select_transports = RC.select_transports
        old_parse_args = RC.parse_args
        old_collect_missing_patterns = RC.collect_missing_patterns
        old_open = builtins.open
        old_makedirs = RC.os.makedirs
        old_stdout = RC.sys.stdout
        old_stderr = RC.sys.stderr
        old_build_result_filename = RC.build_result_filename
        old_single_result_dir = RC.single_result_dir
        old_print_effective_options = RC.print_effective_options
        old_sleep = RC.time.sleep
        old_env = os.environ.copy()
        calls = []
        sleeps = []

        class DummyFile:
            def write(self, _data):
                return 0

            def flush(self):
                return None

            def close(self):
                return None

        try:
            args = type(
                "Args",
                (),
                {
                    "pattern": "PAIR",
                    "runs": 1,
                    "duration": None,
                    "build_dir": "",
                    "pin_cpu": False,
                    "results_dir": "/tmp/ignored",
                    "results_tag": "",
                    "result_file": "",
                    "recv": "callback",
                },
            )()
            os.environ["PERF_TRANSPORT_TRANSITION_MS"] = "23"
            RC.run_single_test = lambda build_dir, current_lib_dir, binary_name, lib_name, pattern, transport, size, timeout_sec, pin_cpu: (
                calls.append((binary_name, pattern, transport, size))
                or RC.RunOutcome(
                    status="success",
                    throughput=1.0,
                    bandwidth=1.0,
                    latency=1.0,
                    latency_p95=1.0,
                    latency_p99=1.0,
                )
            )
            RC.msg_sizes_for_pattern = lambda pattern: [64]
            RC.select_transports = lambda pattern: ["tcp", "tls", "ws"]
            RC.parse_args = lambda: args
            RC.collect_missing_patterns = lambda *args, **kwargs: []
            builtins.open = lambda *args, **kwargs: DummyFile()
            RC.os.makedirs = lambda *args, **kwargs: None
            RC.build_result_filename = lambda mode, tag="": "dummy.txt"
            RC.single_result_dir = lambda root: root
            RC.print_effective_options = lambda *args, **kwargs: None
            RC.time.sleep = lambda seconds: sleeps.append(seconds)

            rc = RC.main()

            self.assertEqual(rc, 0)
            self.assertEqual(
                calls,
                [
                    ("perf_pair", "PAIR", "tcp", 64),
                    ("perf_pair", "PAIR", "tls", 64),
                    ("perf_pair", "PAIR", "ws", 64),
                ],
            )
            self.assertEqual(sleeps, [0.023, 0.023])
        finally:
            RC.run_single_test = old_run_single_test
            RC.msg_sizes_for_pattern = old_msg_sizes_for_pattern
            RC.select_transports = old_select_transports
            RC.parse_args = old_parse_args
            RC.collect_missing_patterns = old_collect_missing_patterns
            builtins.open = old_open
            RC.os.makedirs = old_makedirs
            RC.sys.stdout = old_stdout
            RC.sys.stderr = old_stderr
            RC.build_result_filename = old_build_result_filename
            RC.single_result_dir = old_single_result_dir
            RC.print_effective_options = old_print_effective_options
            RC.time.sleep = old_sleep
            os.environ.clear()
            os.environ.update(old_env)

    def test_single_transport_cooldown_extends_before_inproc_after_websocket(self):
        old_run_single_test = RC.run_single_test
        old_msg_sizes_for_pattern = RC.msg_sizes_for_pattern
        old_select_transports = RC.select_transports
        old_parse_args = RC.parse_args
        old_collect_missing_patterns = RC.collect_missing_patterns
        old_open = builtins.open
        old_makedirs = RC.os.makedirs
        old_stdout = RC.sys.stdout
        old_stderr = RC.sys.stderr
        old_build_result_filename = RC.build_result_filename
        old_single_result_dir = RC.single_result_dir
        old_print_effective_options = RC.print_effective_options
        old_sleep = RC.time.sleep
        old_env = os.environ.copy()
        sleeps = []

        class DummyFile:
            def write(self, _data):
                return 0

            def flush(self):
                return None

            def close(self):
                return None

        try:
            args = type(
                "Args",
                (),
                {
                    "pattern": "PAIR",
                    "runs": 1,
                    "duration": None,
                    "build_dir": "",
                    "pin_cpu": False,
                    "results_dir": "/tmp/ignored",
                    "results_tag": "",
                    "result_file": "",
                    "recv": "callback",
                },
            )()
            os.environ["PERF_TRANSPORT_TRANSITION_MS"] = "23"
            RC.run_single_test = lambda build_dir, current_lib_dir, binary_name, lib_name, pattern, transport, size, timeout_sec, pin_cpu: (
                RC.RunOutcome(
                    status="success",
                    throughput=1.0,
                    bandwidth=1.0,
                    latency=1.0,
                    latency_p95=1.0,
                    latency_p99=1.0,
                )
            )
            RC.msg_sizes_for_pattern = lambda pattern: [64]
            RC.select_transports = lambda pattern: ["wss", "inproc"]
            RC.parse_args = lambda: args
            RC.collect_missing_patterns = lambda *args, **kwargs: []
            builtins.open = lambda *args, **kwargs: DummyFile()
            RC.os.makedirs = lambda *args, **kwargs: None
            RC.build_result_filename = lambda mode, tag="": "dummy.txt"
            RC.single_result_dir = lambda root: root
            RC.print_effective_options = lambda *args, **kwargs: None
            RC.time.sleep = lambda seconds: sleeps.append(seconds)

            rc = RC.main()

            self.assertEqual(rc, 0)
            self.assertEqual(sleeps, [6.0])
        finally:
            RC.run_single_test = old_run_single_test
            RC.msg_sizes_for_pattern = old_msg_sizes_for_pattern
            RC.select_transports = old_select_transports
            RC.parse_args = old_parse_args
            RC.collect_missing_patterns = old_collect_missing_patterns
            builtins.open = old_open
            RC.os.makedirs = old_makedirs
            RC.sys.stdout = old_stdout
            RC.sys.stderr = old_stderr
            RC.build_result_filename = old_build_result_filename
            RC.single_result_dir = old_single_result_dir
            RC.print_effective_options = old_print_effective_options
            RC.time.sleep = old_sleep
            os.environ.clear()
            os.environ.update(old_env)


if __name__ == "__main__":
    unittest.main()
