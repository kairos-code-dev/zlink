from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SAMPLES_DIR = ROOT / "samples"


class SampleAlignmentTests(unittest.TestCase):
    def test_required_sample_files_exist(self):
        expected = [
            "request_reply_async_sample.py",
            "request_reply_callback_sample.py",
            "pair_recv_sample.py",
            "pair_callback_sample.py",
            "dealer_router_recv_sample.py",
            "dealer_router_callback_sample.py",
            "pubsub_recv_sample.py",
            "pubsub_callback_sample.py",
            "stream_recv_sample.py",
            "stream_callback_sample.py",
            "monitor_recv_sample.py",
            "spot_recv_sample.py",
            "spot_callback_sample.py",
            "run_samples.py",
            "run_samples.sh",
        ]
        for name in expected:
            self.assertTrue((SAMPLES_DIR / name).exists(), msg=name)

    def test_samples_avoid_inproc_and_sleep_handshake(self):
        for path in sorted(SAMPLES_DIR.glob("*.py")):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("inproc://", text, msg=path.name)
            self.assertNotIn("sleep(", text, msg=path.name)

    def test_readme_mentions_samples_runner(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("samples/run_samples.sh", readme)

    def test_run_samples_scripts_enumerate_and_summarize_all_samples(self):
        runner = (SAMPLES_DIR / "run_samples.py").read_text(encoding="utf-8")
        shell = (SAMPLES_DIR / "run_samples.sh").read_text(encoding="utf-8")

        self.assertIn("request_reply_async_sample.py", runner)
        self.assertIn("request_reply_callback_sample.py", runner)
        self.assertIn("spot_recv_sample.py", runner)
        self.assertIn("spot_callback_sample.py", runner)
        self.assertIn("pair_recv_sample.py", runner)
        self.assertIn("pair_callback_sample.py", runner)
        self.assertIn("dealer_router_recv_sample.py", runner)
        self.assertIn("dealer_router_callback_sample.py", runner)
        self.assertIn("pubsub_recv_sample.py", runner)
        self.assertIn("pubsub_callback_sample.py", runner)
        self.assertIn("stream_recv_sample.py", runner)
        self.assertIn("stream_callback_sample.py", runner)
        self.assertIn("monitor_recv_sample.py", runner)
        self.assertIn("Summary:", runner)
        self.assertIn("samples/run_samples.py", shell)
