import importlib.util
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
        "recv_mode": "recv",
        "server_ready_timeout_ms": 10000,
        "server_shutdown_timeout_ms": 5000,
        "server_bind_port": 0,
        "transport_transition_ms": 3000,
        "pattern_transition_ms": 3000,
    }


class MultiRunComparisonPolicyTests(unittest.TestCase):
    def test_result_filename_includes_recv_mode(self):
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

    def test_collect_unsupported_patterns_matches_current_multi_matrix(self):
        self.assertEqual(
            RC.collect_unsupported_patterns(
                ["DEALER_DEALER", "GATEWAY", "STREAM"], "recv"
            ),
            [],
        )
        self.assertEqual(
            RC.collect_unsupported_patterns(
                ["DEALER_DEALER", "PUBSUB", "GATEWAY", "SPOT", "STREAM"],
                "callback",
            ),
            ["DEALER_DEALER", "PUBSUB", "GATEWAY"],
        )
        self.assertEqual(
            RC.collect_unsupported_patterns(["SPOT", "STREAM"], "callback"),
            [],
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
            self.assertEqual(RC.pattern_default_hwm("DEALER_DEALER"), 100)
            self.assertEqual(RC.pattern_default_hwm("STREAM"), 10)
            self.assertEqual(RC.pattern_default_io_threads("DEALER_DEALER"), 4)
            self.assertEqual(RC.pattern_default_io_threads("STREAM"), 4)
        finally:
            RC.ALLOW_MULTI = old_allow_multi
            os.environ.clear()
            os.environ.update(old_env)


if __name__ == "__main__":
    unittest.main()
