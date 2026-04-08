import importlib.util
import contextlib
import io
import os
import pathlib
import sys
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[2] / "run_comparison.py"
SPEC = importlib.util.spec_from_file_location("multi_run_comparison", MODULE_PATH)
RC = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
sys.modules[SPEC.name] = RC
SPEC.loader.exec_module(RC)


def multi_args():
    return {
        "num_runs": 1,
        "recv_mode": "callback",
        "server_ready_timeout_ms": 10000,
        "server_shutdown_timeout_ms": 5000,
        "server_bind_port": 0,
        "transport_transition_ms": 3000,
        "pattern_transition_ms": 3000,
    }


def tier1_metrics(value):
    return (
        ("throughput", value),
        ("bandwidth", value),
        ("latency", value),
        (RC.LATENCY_P95_METRIC, value),
        (RC.LATENCY_P99_METRIC, value),
    )

class MultiRunComparisonPolicyTests(unittest.TestCase):
    def test_multi_required_result_metrics_are_tier1_only(self):
        self.assertEqual(RC.REQUIRED_RESULT_METRICS, tuple(name for name, _ in tier1_metrics(1.0)))
        self.assertEqual(RC.REQUIRED_RESULT_METRIC_COUNT, 5)

    def test_multi_default_msg_sizes_include_64b(self):
        self.assertEqual(
            RC.MSG_SIZES,
            [64, 256, 1024, 65536, 131072, 262144],
        )
        self.assertEqual(
            RC.STREAM_MSG_SIZES,
            [64, 256, 1024, 65536],
        )

    def test_result_filename_uses_current_mode_label(self):
        old_value = os.environ.get("PERF_RECV_MODE")
        try:
            os.environ["PERF_RECV_MODE"] = "callback"
            self.assertRegex(
                RC.build_result_filename("tag"),
                r"^perf_[a-z]+_callback_\d{8}_\d{6}_tag\.txt$",
            )
        finally:
            if old_value is None:
                os.environ.pop("PERF_RECV_MODE", None)
            else:
                os.environ["PERF_RECV_MODE"] = old_value

    def test_expand_pattern_aliases_ordered_preserves_request_order(self):
        self.assertEqual(
            RC.expand_pattern_aliases_ordered(
                ["DEALER_DEALER", "STREAMS"]
            ),
            ["DEALER_DEALER", "STREAM"],
        )

    def test_collect_unsupported_patterns_matches_current_callback_matrix(self):
        self.assertEqual(
            RC.collect_unsupported_patterns(
                ["SPOT", "STREAM"], "callback"
            ),
            [],
        )
        self.assertEqual(
            RC.collect_unsupported_patterns(
                ["DEALER_DEALER", "PUBSUB", "SPOT", "STREAM"],
                "callback",
            ),
            ["DEALER_DEALER", "PUBSUB"],
        )

    def test_multi_connect_concurrency_header_uses_multi_env_name(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_env = os.environ.copy()
        try:
            RC.ALLOW_MULTI = True
            os.environ["PERF_MULTI_CONNECT_CONCURRENCY"] = "321"
            items = dict(
                RC.build_effective_option_items(multi_args(), ["DEALER_DEALER"])
            )
            self.assertEqual(items["connect_concurrency"], "321")
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            os.environ.clear()
            os.environ.update(old_env)

    def test_multi_defaults_do_not_require_internal_default_envs(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_env = os.environ.copy()
        try:
            RC.ALLOW_MULTI = True
            for key in (
                "PERF_MULTI_DEFAULT_CLIENTS",
                "PERF_MULTI_DEFAULT_STREAM_CLIENTS",
                "PERF_MULTI_DEFAULT_HWM",
                "PERF_MULTI_DEFAULT_STREAM_HWM",
                "PERF_MULTI_STREAM_DEFAULT_IO_THREADS",
            ):
                os.environ.pop(key, None)
            self.assertEqual(RC.pattern_default_clients("DEALER_DEALER"), 100)
            self.assertEqual(RC.pattern_default_clients("STREAM"), 10000)
            self.assertEqual(RC.pattern_default_hwm("DEALER_DEALER"), 1000)
            self.assertEqual(RC.pattern_default_hwm("STREAM"), 1000)
            self.assertEqual(RC.pattern_default_io_threads("DEALER_DEALER"), 4)
            self.assertEqual(RC.pattern_default_io_threads("STREAM"), 4)
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            os.environ.clear()
            os.environ.update(old_env)

    def test_multi_split_runner_isolates_each_size_case(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_split = RC.run_sizes_test_split
        calls = []
        try:
            RC.ALLOW_MULTI = True

            def fake_split(server_name, client_name, lib_name, transport, sizes,
                           pattern_name, result_line_callback=None):
                calls.append(
                    (server_name, client_name, lib_name, transport,
                     list(sizes), pattern_name)
                )
                return {
                    "status": "success",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "reason": "",
                    "warnings": [],
                }

            RC.run_sizes_test_split = fake_split
            outcome = RC.run_sizes_test(
                "ignored",
                "current",
                "tcp",
                [64, 256, 1024],
                "SPOT",
            )

            self.assertEqual(outcome["status"], "success")
            self.assertEqual(
                calls,
                [
                    ("comp_src_spot_server", "comp_src_spot_client",
                     "current", "tcp", [64], "SPOT"),
                    ("comp_src_spot_server", "comp_src_spot_client",
                     "current", "tcp", [256], "SPOT"),
                    ("comp_src_spot_server", "comp_src_spot_client",
                     "current", "tcp", [1024], "SPOT"),
                ],
            )
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            RC.run_sizes_test_split = old_split

    def test_multi_stream_runner_isolates_each_size_case(self):
        old_stream = RC.run_sizes_test_stream_shared
        calls = []
        try:
            def fake_stream(server_name, lib_name, transport, sizes,
                            pattern_name, result_line_callback=None):
                calls.append(
                    (server_name, lib_name, transport, list(sizes), pattern_name)
                )
                return {
                    "status": "success",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "reason": "",
                    "warnings": [],
                }

            RC.run_sizes_test_stream_shared = fake_stream
            outcome = RC.run_sizes_test(
                "ignored",
                "current",
                "wss",
                [64, 256],
                "STREAM",
            )

            self.assertEqual(outcome["status"], "success")
            self.assertEqual(
                calls,
                [
                    ("comp_src_stream_server", "current", "wss", [64], "STREAM"),
                    ("comp_src_stream_server", "current", "wss", [256], "STREAM"),
                ],
            )
        finally:
            RC.run_sizes_test_stream_shared = old_stream

    def test_multi_size_transition_sleep_applies_only_between_cases(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_split = RC.run_sizes_test_split
        old_sleep = RC.time.sleep
        old_env = os.environ.copy()
        calls = []
        sleeps = []
        try:
            RC.ALLOW_MULTI = True
            os.environ["PERF_MULTI_SIZE_TRANSITION_MS"] = "17"

            def fake_split(server_name, client_name, lib_name, transport, sizes,
                           pattern_name, result_line_callback=None):
                calls.append(list(sizes))
                return {
                    "status": "success",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "reason": "",
                    "warnings": [],
                }

            def fake_sleep(seconds):
                sleeps.append(seconds)

            RC.run_sizes_test_split = fake_split
            RC.time.sleep = fake_sleep

            outcome = RC.run_sizes_test(
                "ignored",
                "current",
                "tcp",
                [64, 256, 1024],
                "SPOT",
            )

            self.assertEqual(outcome["status"], "success")
            self.assertEqual(calls, [[64], [256], [1024]])
            self.assertEqual(sleeps, [0.017, 0.017])
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            RC.run_sizes_test_split = old_split
            RC.time.sleep = old_sleep
            os.environ.clear()
            os.environ.update(old_env)

    def test_multi_transport_cooldown_runs_only_between_transports(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_run_sizes_test = RC.run_sizes_test
        old_sleep = RC.time.sleep
        old_env = os.environ.copy()
        calls = []
        sleeps = []
        try:
            RC.ALLOW_MULTI = True
            os.environ["PERF_RUN_COOLDOWN_MS"] = "0"
            os.environ["PERF_TRANSPORT_TRANSITION_MS"] = "23"

            def fake_run_sizes_test(binary_name, lib_name, transport, sizes,
                                    pattern_name, result_line_callback=None):
                calls.append((transport, list(sizes), pattern_name))
                if result_line_callback is not None:
                    for size in sizes:
                        for metric_name, value in tier1_metrics(1.0):
                            result_line_callback(transport, size, metric_name, value)
                return {
                    "status": "success",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "reason": "",
                    "warnings": [],
                }

            def fake_sleep(seconds):
                sleeps.append(seconds)

            RC.run_sizes_test = fake_run_sizes_test
            RC.time.sleep = fake_sleep

            final_stats, failures = RC.collect_data(
                "ignored",
                "current",
                "SPOT",
                1,
                transports=["tcp", "tls", "ws"],
                table_lines=[],
            )

            self.assertEqual(failures, [])
            self.assertEqual(
                calls,
                [
                    ("tcp", RC.MSG_SIZES, "SPOT"),
                    ("tls", RC.MSG_SIZES, "SPOT"),
                    ("ws", RC.MSG_SIZES, "SPOT"),
                ],
            )
            self.assertEqual(sleeps, [0.023, 0.023])
            self.assertIn("tcp|256|throughput", final_stats)
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            RC.run_sizes_test = old_run_sizes_test
            RC.time.sleep = old_sleep
            os.environ.clear()
            os.environ.update(old_env)

    def test_multi_collect_data_reports_each_size_separately(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_run_sizes_test = RC.run_sizes_test
        old_env = os.environ.copy()
        try:
            RC.ALLOW_MULTI = True
            os.environ["PERF_RUN_COOLDOWN_MS"] = "0"
            os.environ["PERF_TRANSPORT_TRANSITION_MS"] = "0"

            def fake_run_sizes_test(binary_name, lib_name, transport, sizes,
                                    pattern_name, result_line_callback=None):
                for size in sizes:
                    if result_line_callback is not None:
                        for metric_name, value in tier1_metrics(1.0):
                            result_line_callback(transport, size, metric_name, value)
                return {
                    "status": "success",
                    "parsed": {
                        "tcp|64|throughput": 1.0,
                        "tcp|64|bandwidth": 1.0,
                        "tcp|64|latency": 1.0,
                        "tcp|64|latency_p95": 1.0,
                        "tcp|64|latency_p99": 1.0,
                        "tcp|256|throughput": 1.0,
                        "tcp|256|bandwidth": 1.0,
                        "tcp|256|latency": 1.0,
                        "tcp|256|latency_p95": 1.0,
                        "tcp|256|latency_p99": 1.0,
                    },
                    "timed_out": False,
                    "returncode": 0,
                    "reason": "",
                    "warnings": [],
                }

            RC.run_sizes_test = fake_run_sizes_test

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                RC.collect_data(
                    "ignored",
                    "current",
                    "PUBSUB",
                    1,
                    transports=["tcp"],
                    table_lines=[],
                )

            output = stdout.getvalue()
            self.assertIn("Testing tcp | 64B:", output)
            self.assertIn("Testing tcp | 256B:", output)
            self.assertNotIn("Testing tcp | 64B,256B:", output)
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            RC.run_sizes_test = old_run_sizes_test
            os.environ.clear()
            os.environ.update(old_env)

    def test_multi_collect_data_omits_legacy_queue_metrics(self):
        old_allow_multi = RC.ALLOW_MULTI
        old_run_sizes_test = RC.run_sizes_test
        old_env = os.environ.copy()
        try:
            RC.ALLOW_MULTI = True
            os.environ["PERF_RUN_COOLDOWN_MS"] = "0"
            os.environ["PERF_TRANSPORT_TRANSITION_MS"] = "0"

            def fake_run_sizes_test(binary_name, lib_name, transport, sizes,
                                    pattern_name, result_line_callback=None):
                for size in sizes:
                    if result_line_callback is not None:
                        for metric_name, value in tier1_metrics(1.0):
                            result_line_callback(transport, size, metric_name, value)
                return {
                    "status": "success",
                    "parsed": {
                        "tcp|64|throughput": 1.0,
                        "tcp|64|bandwidth": 1.0,
                        "tcp|64|latency": 1.0,
                        "tcp|64|latency_p95": 1.0,
                        "tcp|64|latency_p99": 1.0,
                    },
                    "timed_out": False,
                    "returncode": 0,
                    "reason": "",
                    "warnings": [],
                }

            RC.run_sizes_test = fake_run_sizes_test

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                RC.collect_data(
                    "ignored",
                    "current",
                    "PUBSUB",
                    1,
                    transports=["tcp"],
                    table_lines=[],
                )

            output = stdout.getvalue()
            for metric_name in ("Q.Snd.Max", "Q.Rcv.Max", "Q.Rcv.End"):
                self.assertNotIn(metric_name, output)
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            RC.run_sizes_test = old_run_sizes_test
            os.environ.clear()
            os.environ.update(old_env)


if __name__ == "__main__":
    unittest.main()
