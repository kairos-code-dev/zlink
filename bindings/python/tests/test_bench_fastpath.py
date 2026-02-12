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
            self.assertIsNone(
                bench_common.make_cext_gateway_send_many_const(None, "svc", payload)
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

    def test_cext_gateway_and_spot_many(self):
        if bench_common.FASTPATH_CEXT is None:
            self.skipTest("fastpath C-extension not available")
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        suffix = str(int(time.time() * 1000))
        reg_pub = f"inproc://py-fastpath-gw-pub-{suffix}"
        reg_router = f"inproc://py-fastpath-gw-router-{suffix}"
        provider_ep = f"inproc://py-fastpath-gw-provider-{suffix}"
        registry = None
        discovery = None
        receiver = None
        router = None
        gateway = None

        node_pub = None
        node_sub = None
        spot_pub = None
        spot_sub = None

        try:
            registry = zlink.Registry(ctx)
            registry.set_heartbeat(5000, 60000)
            registry.set_endpoints(reg_pub, reg_router)
            registry.start()

            discovery = zlink.Discovery(ctx, int(zlink.ServiceType.GATEWAY))
            discovery.connect_registry(reg_pub)
            discovery.subscribe("svc")

            receiver = zlink.Receiver(ctx)
            receiver.bind(provider_ep)
            receiver.connect_registry(reg_router)
            receiver.register("svc", provider_ep, 1)
            router = receiver.router_socket()

            gateway = zlink.Gateway(ctx, discovery)
            self.assertTrue(
                bench_common.wait_until(lambda: discovery.receiver_count("svc") > 0, 5000)
            )
            self.assertTrue(
                bench_common.wait_until(lambda: gateway.connection_count("svc") > 0, 5000)
            )
            time.sleep(0.1)

            gw_payload = b"gateway-fastpath"
            rid_buf = bytearray(256)
            data_buf = bytearray(64)
            gw_send_many = bench_common.make_cext_gateway_send_many_const(
                gateway, "svc", gw_payload
            )
            gw_recv_many = bench_common.make_cext_recv_pair_many_into(
                router, rid_buf, data_buf
            )
            self.assertIsNotNone(gw_send_many)
            self.assertIsNotNone(gw_recv_many)

            count = 64
            send_none = int(zlink.SendFlag.NONE)
            recv_none = int(zlink.ReceiveFlag.NONE)
            self.assertEqual(gw_send_many(count, send_none), count)
            self.assertEqual(gw_recv_many(count, recv_none), count)
            self.assertEqual(bytes(data_buf[: len(gw_payload)]), gw_payload)

            node_pub = zlink.SpotNode(ctx)
            node_sub = zlink.SpotNode(ctx)
            spot_ep = f"inproc://py-fastpath-spot-{suffix}"
            node_pub.bind(spot_ep)
            node_sub.connect_peer_pub(spot_ep)
            spot_pub = zlink.Spot(node_pub)
            spot_sub = zlink.Spot(node_sub)
            spot_sub.subscribe("bench")
            time.sleep(0.2)

            spot_payload = b"spot-fastpath"
            spot_publish_many = bench_common.make_cext_spot_publish_many_const(
                spot_pub, "bench", spot_payload
            )
            spot_recv_many = bench_common.make_cext_spot_recv_many(spot_sub)
            self.assertIsNotNone(spot_publish_many)
            self.assertIsNotNone(spot_recv_many)

            self.assertEqual(spot_publish_many(count, send_none), count)
            self.assertEqual(spot_recv_many(count, recv_none), count)

            spot_pub.publish("bench", [spot_payload], send_none)
            topic, parts = spot_sub.recv(recv_none)
            self.assertEqual(topic, "bench")
            self.assertEqual(parts, [spot_payload])
        finally:
            if spot_sub is not None:
                spot_sub.close()
            if spot_pub is not None:
                spot_pub.close()
            if node_sub is not None:
                node_sub.close()
            if node_pub is not None:
                node_pub.close()
            if gateway is not None:
                gateway.close()
            if router is not None:
                router.close()
            if receiver is not None:
                receiver.close()
            if discovery is not None:
                discovery.close()
            if registry is not None:
                registry.close()
            ctx.close()


if __name__ == "__main__":
    unittest.main()
