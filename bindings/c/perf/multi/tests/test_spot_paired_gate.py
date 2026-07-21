import importlib.util
import contextlib
import io
import pathlib
import sys
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[2] / "run_spot_paired_gate.py"
SPEC = importlib.util.spec_from_file_location("spot_paired_gate", MODULE_PATH)
GATE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
sys.modules[SPEC.name] = GATE
SPEC.loader.exec_module(GATE)


class SpotPairedGateTests(unittest.TestCase):
    def test_order_alternates_within_each_pair(self):
        self.assertEqual(
            GATE.paired_order("SPOT_REQREP", 0),
            ("SPOT_REQREP", "ROUTER_ROUTER_REQREP"),
        )
        self.assertEqual(
            GATE.paired_order("SPOT_REQREP", 1),
            ("ROUTER_ROUTER_REQREP", "SPOT_REQREP"),
        )

    def test_complete_report_requires_all_five_metrics(self):
        lines = [
            (
                "SPOT_DIAG,current,MULTI_SPOT_PUBSUB,tcp,64,role=hub,"
                "pending_application_messages=0,pending_bytes=0,"
                "multicast_dropped_targets=0"
            ),
            "RESULT,current,MULTI_SPOT_PUBSUB,tcp,64,throughput,90",
            "RESULT,current,MULTI_SPOT_PUBSUB,tcp,64,bandwidth,5.76",
            "RESULT,current,MULTI_SPOT_PUBSUB,tcp,64,latency,1.0",
            "RESULT,current,MULTI_SPOT_PUBSUB,tcp,64,latency_p95,1.1",
            "RESULT,current,MULTI_SPOT_PUBSUB,tcp,64,latency_p99,1.2",
            "- status: complete",
        ]
        sample = GATE.parse_report(
            "\n".join(lines), "SPOT_PUBSUB", "tcp", 64
        )
        self.assertEqual(sample.metrics["throughput"], 90.0)
        self.assertEqual(sample.multicast_dropped_targets, 0)

        with self.assertRaisesRegex(ValueError, "latency_p99"):
            GATE.parse_report(
                "\n".join(lines[:-2] + [lines[-1]]),
                "SPOT_PUBSUB",
                "tcp",
                64,
            )

    def test_gate_checks_throughput_and_all_latency_ratios(self):
        passed = GATE.evaluate_cell(
            "SPOT_SENDSEND",
            {
                "throughput": 90.0,
                "bandwidth": 1.0,
                "latency": 1.25,
                "latency_p95": 2.5,
                "latency_p99": 3.75,
            },
            {
                "throughput": 100.0,
                "bandwidth": 1.0,
                "latency": 1.0,
                "latency_p95": 2.0,
                "latency_p99": 3.0,
            },
            0.90,
            1.25,
        )
        self.assertEqual(passed["status"], "pass")

        failed = GATE.evaluate_cell(
            "SPOT_SENDSEND",
            {
                "throughput": 89.0,
                "bandwidth": 1.0,
                "latency": 1.0,
                "latency_p95": 2.0,
                "latency_p99": 3.76,
            },
            {
                "throughput": 100.0,
                "bandwidth": 1.0,
                "latency": 1.0,
                "latency_p95": 2.0,
                "latency_p99": 3.0,
            },
            0.90,
            1.25,
        )
        self.assertEqual(failed["status"], "fail")
        self.assertEqual(len(failed["failures"]), 2)

    def test_gate_rejects_multicast_target_drops(self):
        cell = GATE.evaluate_cell(
            "SPOT_PUBSUB",
            {
                "throughput": 100.0,
                "bandwidth": 1.0,
                "latency": 1.0,
                "latency_p95": 1.0,
                "latency_p99": 1.0,
            },
            {
                "throughput": 100.0,
                "bandwidth": 1.0,
                "latency": 1.0,
                "latency_p95": 1.0,
                "latency_p99": 1.0,
            },
            0.90,
            1.25,
            multicast_dropped_targets=1,
        )
        self.assertEqual(cell["status"], "fail")
        self.assertIn(
            "multicast_dropped_targets=1>0",
            cell["failures"],
        )

    def test_command_fixes_matching_one_thread_resource_condition(self):
        command = GATE.build_case_command(
            pathlib.Path("/repo/run.sh"),
            "ROUTER_ROUTER_REQREP",
            "wss",
            4096,
            100,
            5,
            pathlib.Path("/tmp/results"),
            "paired",
        )
        self.assertEqual(command[command.index("--server-io-threads") + 1], "1")
        self.assertEqual(command[command.index("--client-io-threads") + 1], "1")
        self.assertEqual(command[command.index("--runs") + 1], "1")

    def test_gnu_time_wrap_is_outside_the_existing_runner(self):
        command = ["/repo/run.sh", "--reuse-build"]
        expected_output = pathlib.Path("/tmp/raw.txt")
        captured = {}
        old_run = GATE.subprocess.run
        try:
            def fake_run(final_command, **kwargs):
                captured["command"] = final_command
                return type(
                    "Completed",
                    (),
                    {"returncode": 0, "stdout": "- status: complete\n"},
                )()

            GATE.subprocess.run = fake_run
            with contextlib.redirect_stdout(io.StringIO()):
                GATE.run_case(
                    command,
                    expected_output,
                    None,
                    pathlib.Path("/tmp/raw.time.txt"),
                )
        finally:
            GATE.subprocess.run = old_run
            expected_output.unlink(missing_ok=True)

        self.assertEqual(
            captured["command"][:5],
            ["/usr/bin/time", "-v", "-o", "/tmp/raw.time.txt", "/repo/run.sh"],
        )


if __name__ == "__main__":
    unittest.main()
