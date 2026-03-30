import sys
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PY_SRC = ROOT / "bindings" / "python" / "src"
BENCH_DIR = ROOT / "bindings" / "python" / "perf" / "single"
sys.path.insert(0, str(PY_SRC))
sys.path.insert(0, str(BENCH_DIR))

import zlink

try:
    import perf_bench_common as bench_common
except ModuleNotFoundError:
    bench_common = None


class BenchFastpathTests(unittest.TestCase):
    @staticmethod
    def _wait_for_socket_event(sock, events, timeout_ms):
        with zlink.Poller() as poller:
            poller.add_socket(sock, events)
            ready = poller.poll(timeout_ms)
        return bool(ready)

    @classmethod
    def setUpClass(cls):
        if bench_common is None:
            raise unittest.SkipTest("perf_bench_common is not available")

    def test_cext_wrappers_return_none_when_fastpath_unavailable(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        a = zlink.Socket(ctx, zlink.SocketType.PAIR)
        b = zlink.Socket(ctx, zlink.SocketType.PAIR)
        endpoint = f"inproc://py-fastpath-off-{int(time.time() * 1000)}"
        a.bind(endpoint)
        b.connect(endpoint)
        self.assertTrue(self._wait_for_socket_event(b, zlink.PollEvent.POLLOUT, 3000))

        saved = bench_common.FASTPATH_CEXT
        bench_common.FASTPATH_CEXT = None
        try:
            payload = b"abc"
            recv_buf = bytearray(16)
            self.assertIsNone(bench_common.make_cext_send_many_const(b, payload))
            self.assertIsNone(bench_common.make_cext_recv_many_into(a, recv_buf))
            self.assertIsNone(
                bench_common.make_cext_send_routed_many_const(
                    b, b"RID", payload
                )
            )
            self.assertIsNone(
                bench_common.make_cext_recv_pair_many_into(a, recv_buf, recv_buf)
            )
            self.assertIsNone(
                bench_common.make_cext_recv_pair_drain_into(a, recv_buf, recv_buf)
            )
            self.assertIsNone(
                bench_common.make_cext_spot_publish_many_const(None, "bench", payload)
            )
            self.assertIsNone(
                bench_common.make_cext_spot_recv_many(None)
            )
        finally:
            bench_common.FASTPATH_CEXT = saved
            a.close()
            b.close()
            ctx.close()

    def test_cext_pair_send_recv_many(self):
        if bench_common.FASTPATH_CEXT is None:
            self.skipTest("fastpath C-extension not available")
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        a = zlink.Socket(ctx, zlink.SocketType.PAIR)
        b = zlink.Socket(ctx, zlink.SocketType.PAIR)
        endpoint = f"inproc://py-fastpath-pair-{int(time.time() * 1000)}"
        a.bind(endpoint)
        b.connect(endpoint)
        self.assertTrue(self._wait_for_socket_event(b, zlink.PollEvent.POLLOUT, 3000))

        payload = b"fastpath"
        recv_buf = bytearray(64)
        send_many = bench_common.make_cext_send_many_const(b, payload)
        recv_many = bench_common.make_cext_recv_many_into(a, recv_buf)
        self.assertIsNotNone(send_many)
        self.assertIsNotNone(recv_many)

        count = 64
        send_none = 0
        recv_none = 0
        self.assertEqual(send_many(count, send_none), count)
        self.assertEqual(recv_many(count, recv_none), count)
        self.assertEqual(bytes(recv_buf[: len(payload)]), payload)

        a.close()
        b.close()
        ctx.close()

    def test_cext_router_pair_drain(self):
        if bench_common.FASTPATH_CEXT is None:
            self.skipTest("fastpath C-extension not available")
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        router = zlink.Socket(ctx, zlink.SocketType.ROUTER)
        dealer = zlink.Socket(ctx, zlink.SocketType.DEALER)
        endpoint = f"inproc://py-fastpath-drain-{int(time.time() * 1000)}"

        dealer.set_routing_id(b"CLIENT")
        router.bind(endpoint)
        dealer.connect(endpoint)
        self.assertTrue(
            self._wait_for_socket_event(dealer, zlink.PollEvent.POLLOUT, 3000)
        )

        payload = b"x" * 16
        rid_buf = bytearray(256)
        data_buf = bytearray(64)
        send_many = bench_common.make_cext_send_many_const(dealer, payload)
        recv_drain = bench_common.make_cext_recv_pair_drain_into(router, rid_buf, data_buf)
        self.assertIsNotNone(send_many)
        self.assertIsNotNone(recv_drain)

        count = 96
        send_none = 0
        self.assertEqual(send_many(count, send_none), count)
        self.assertTrue(self._wait_for_socket_event(router, zlink.PollEvent.POLLIN, 3000))
        self.assertEqual(recv_drain(count), count)
        self.assertEqual(bytes(data_buf[: len(payload)]), payload)

        router.close()
        dealer.close()
        ctx.close()

    def test_cext_spot_many(self):
        if bench_common.FASTPATH_CEXT is None:
            self.skipTest("fastpath C-extension not available")
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        spot_pub = None
        spot_sub = None

        try:
            count = 64
            send_none = 0
            recv_none = 0
            spot_pub = zlink.Spot(ctx)
            spot_sub = zlink.Spot(ctx)
            spot_sub.set_subscription("bench")

            spot_payload = b"spot-fastpath"
            spot_publish_many = bench_common.make_cext_spot_publish_many_const(
                spot_pub, "bench", spot_payload
            )
            spot_recv_many = bench_common.make_cext_spot_recv_many(spot_sub)
            self.assertIsNotNone(spot_publish_many)
            self.assertIsNotNone(spot_recv_many)

            self.assertEqual(spot_publish_many(count, send_none), count)
            self.assertEqual(spot_recv_many(count, recv_none), count)

            spot_pub.publish("bench", [spot_payload])
            self.assertTrue(
                self._wait_for_socket_event(spot_sub, zlink.PollEvent.POLLIN, 3000)
            )
            with spot_sub.recv() as received:
                self.assertEqual(received.topic, b"bench")
                self.assertEqual(received.to_bytes_list(), [spot_payload])
        finally:
            if spot_sub is not None:
                spot_sub.close()
            if spot_pub is not None:
                spot_pub.close()
            ctx.close()


if __name__ == "__main__":
    unittest.main()
