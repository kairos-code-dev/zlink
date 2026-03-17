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
                ["STREAM_CALLBACK", "DEALER_DEALER", "STREAM_CALLBACK"]
            ),
            ["STREAM_CALLBACK", "DEALER_DEALER"],
        )


if __name__ == "__main__":
    unittest.main()
