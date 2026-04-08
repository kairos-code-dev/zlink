from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReadmeAlignmentTests(unittest.TestCase):
    def test_python_readme_exists_and_references_binding_policy(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("bindings/README.md", readme)
        self.assertIn("core/include/zlink.h", readme)
        self.assertIn("multipart-only", readme)
        self.assertIn("capability matrix", readme)
        self.assertIn("try_send", readme)
        self.assertIn("try_recv", readme)
        self.assertIn("ContextOptions", readme)
        self.assertIn("getProperty", readme)
        self.assertIn("refCount", readme)
        self.assertIn("on_receive", readme)
        self.assertIn("on_subscribe", readme)
        self.assertIn("on_send_ready", readme)
        self.assertIn("on_event", readme)
        self.assertIn("monitor_open()", readme)
        self.assertIn("async context manager cleanup", readme)
        self.assertIn("Python-managed dispatcher thread", readme)
        self.assertIn("explicit error instead of", readme)
        self.assertIn("attach_discovery", readme)
        self.assertIn("MemberPeerEntry", readme)
        self.assertIn("RegistryQueryClient", readme)
        self.assertIn("Spot", readme)
        self.assertIn("XPubSocket", readme)
        self.assertIn("tests/run_tests.sh", readme)

    def test_test_runner_script_exists_in_tests_directory(self):
        script = ROOT / "tests" / "run_tests.sh"
        self.assertTrue(script.exists())

    def test_perf_readme_references_perf_policy_and_actual_transport_matrix(self):
        readme = (ROOT / "perf" / "README.md").read_text(encoding="utf-8")
        self.assertIn("bindings/README.md", readme)
        self.assertIn("doc/perf/PERF_POLICY.md", readme)
        self.assertIn("doc/perf/PERF_SINGLE_TEST_POLICY.md", readme)
        self.assertIn("doc/perf/PERF_MULTI_TEST_POLICY.md", readme)
        self.assertIn("`PAIR`: `tcp`", readme)
        self.assertIn("`PUBSUB`: `inproc`", readme)
        self.assertIn("MULTI_STREAM", readme)
        self.assertIn("RESULT,current", readme)
        self.assertIn("## Effective Options (start)", readme)
        self.assertIn("--msg-sizes", readme)
        self.assertIn("perf_<platform>_<recv_mode>", readme)

    def test_multi_perf_readme_documents_result_names_and_callback_subset(self):
        readme = (ROOT / "perf" / "multi" / "README.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("MULTI_PUBSUB", readme)
        self.assertIn("MULTI_DEALER_ROUTER", readme)
        self.assertIn("MULTI_ROUTER_ROUTER", readme)
        self.assertIn("MULTI_SPOT", readme)
        self.assertIn("callback", readme)
        self.assertIn("SPOT", readme)
        self.assertIn("STREAM", readme)
        self.assertIn("results/multi/report/perf_<platform>_<recv_mode>", readme)
