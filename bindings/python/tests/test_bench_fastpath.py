import sys
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PY_SRC = ROOT / "bindings" / "python" / "src"
BENCH_DIR = ROOT / "bindings" / "python" / "benchwithzlink"
sys.path.insert(0, str(PY_SRC))
sys.path.insert(0, str(BENCH_DIR))

import zlink
import bench_common


class BenchFastpathTests(unittest.TestCase):
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
        time.sleep(0.05)

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
        time.sleep(0.05)

        payload = b"fastpath"
        recv_buf = bytearray(64)
        send_many = bench_common.make_cext_send_many_const(b, payload)
        recv_many = bench_common.make_cext_recv_many_into(a, recv_buf)
        self.assertIsNotNone(send_many)
        self.assertIsNotNone(recv_many)

        count = 64
        send_none = int(zlink.SendFlag.NONE)
        recv_none = int(zlink.ReceiveFlag.NONE)
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

        dealer.setsockopt(int(zlink.SocketOption.ROUTING_ID), b"CLIENT")
        router.bind(endpoint)
        dealer.connect(endpoint)
        time.sleep(0.05)

        payload = b"x" * 16
        rid_buf = bytearray(256)
        data_buf = bytearray(64)
        send_many = bench_common.make_cext_send_many_const(dealer, payload)
        recv_drain = bench_common.make_cext_recv_pair_drain_into(router, rid_buf, data_buf)
        self.assertIsNotNone(send_many)
        self.assertIsNotNone(recv_drain)

        count = 96
        send_none = int(zlink.SendFlag.NONE)
        self.assertEqual(send_many(count, send_none), count)

        received = 0
        deadline = time.time() + 3.0
        while received < count and time.time() < deadline:
            if not bench_common.wait_for_input(router, 100):
                continue
            drained = recv_drain(count - received)
            if drained <= 0:
                continue
            received += drained

        self.assertEqual(received, count)
        self.assertEqual(bytes(data_buf[: len(payload)]), payload)

        router.close()
        dealer.close()
        ctx.close()


if __name__ == "__main__":
    unittest.main()
