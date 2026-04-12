import asyncio
import socket
import threading
import unittest

import zlink


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


class CoreApiAlignmentTests(unittest.TestCase):
    def test_public_surface_uses_canonical_names_only(self):
        self.assertFalse(hasattr(zlink, "SendResult"))
        self.assertFalse(hasattr(zlink, "SendFlag"))
        self.assertFalse(hasattr(zlink, "ReceiveFlag"))
        self.assertFalse(hasattr(zlink.PairSocket, "try_send"))
        self.assertFalse(hasattr(zlink.PairSocket, "try_recv"))
        self.assertFalse(hasattr(zlink.PubSocket, "try_publish"))
        self.assertFalse(hasattr(zlink.SubSocket, "try_subscribe"))
        self.assertFalse(hasattr(zlink.XPubSocket, "try_receive_subscription_event"))
        self.assertFalse(hasattr(zlink.MonitorSocket, "try_recv"))
        self.assertFalse(hasattr(zlink.ServiceMonitor, "try_recv"))
        self.assertFalse(hasattr(zlink.RequestDealer, "try_request"))
        self.assertFalse(hasattr(zlink.RequestRouter, "try_request"))
        self.assertFalse(hasattr(zlink.RequestRouter, "try_reply"))
        self.assertFalse(hasattr(zlink.Spot, "try_subscribe"))
        self.assertFalse(hasattr(zlink.Spot, "try_publish"))
        self.assertTrue(hasattr(zlink, "SendFlags"))
        self.assertTrue(hasattr(zlink, "RecvFlags"))
        self.assertTrue(hasattr(zlink, "SubmitResult"))
        self.assertTrue(hasattr(zlink, "RequestResult"))
        self.assertTrue(hasattr(zlink, "RecvResult"))

    def test_context_options_use_snake_case(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            self.assertTrue(hasattr(ctx.options, "io_threads"))
            ctx.options.io_threads = 1
            self.assertEqual(ctx.options.io_threads, 1)
            ctx.options.max_sockets = 1024
            self.assertEqual(ctx.options.max_sockets, 1024)
            self.assertFalse(hasattr(ctx.options, "ioThreads"))
            self.assertFalse(hasattr(ctx.options, "maxSockets"))

    def test_pair_send_and_recv_use_flags(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                with zlink.PairSocket(ctx) as receiver:
                    endpoint = "inproc://py-canonical-pair"
                    sender.bind(endpoint)
                    receiver.connect(endpoint)
                    sender.send(b"payload", flags=zlink.SendFlags.NONE)
                    with receiver.recv(flags=zlink.RecvFlags.NONE) as received:
                        self.assertEqual(received.to_bytes_list(), [b"payload"])

    def test_nonblocking_send_raises_submit_error(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sender:
                sender.bind("inproc://py-canonical-backpressure")
                with self.assertRaises(zlink.SubmitError) as cm:
                    sender.send(b"payload", flags=zlink.SendFlags.DONT_WAIT)
                self.assertIsInstance(cm.exception.result, zlink.SubmitResult)

    def test_pubsub_canonical_roundtrip(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with zlink.SubSocket(ctx) as subscriber:
                    endpoint = "inproc://py-canonical-pubsub"
                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    subscriber.set_subscription(b"topic")
                    publisher.publish(b"topic", [b"payload"], flags=zlink.SendFlags.NONE)
                    with subscriber.subscribe(flags=zlink.RecvFlags.NONE) as received:
                        self.assertEqual(received.topic, b"topic")
                        self.assertEqual(received.to_bytes_list(), [b"payload"])

    def test_monitor_surface_uses_recv_and_snapshot(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PairSocket(ctx) as sock:
                with sock.monitor_open() as monitor:
                    self.assertFalse(hasattr(monitor, "try_recv"))
                    snapshot = monitor.snapshot()
                    self.assertIsInstance(snapshot, zlink.MonitorSnapshot)

    def test_request_reply_canonical_roundtrip(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        async def scenario():
            with zlink.RouterSocket(ctx) as router_socket:
                with zlink.DealerSocket(ctx) as dealer_socket:
                    router = zlink.RequestRouter(router_socket)
                    dealer = zlink.RequestDealer(dealer_socket)
                    endpoint = "inproc://py-canonical-request-reply"
                    router_socket.bind(endpoint)
                    dealer_socket.connect(endpoint)

                    def on_receive(received):
                        router.reply(received.routing_id, received.request_seq, [b"pong"])

                    router.on_receive(on_receive)
                    reply = await dealer.request([b"ping"], timeout=2.0)
                    try:
                        self.assertEqual(reply.to_bytes_list(), [b"pong"])
                    finally:
                        reply.close()
                    router.close()
                    dealer.close()

        asyncio.run(scenario())

    def test_spot_surface_and_pubsub_roundtrip(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    self.assertTrue(hasattr(spot, "send_to_spot"))
                    self.assertTrue(hasattr(spot, "request_to_spot"))
                    self.assertTrue(hasattr(spot, "reply_to_spot"))
                    self.assertTrue(hasattr(spot, "send_to_router"))
                    self.assertTrue(hasattr(spot, "request_to_router"))
                    self.assertTrue(hasattr(spot, "reply_to_router"))
                    self.assertTrue(hasattr(spot, "recv_routed"))
                    self.assertTrue(hasattr(spot, "on_routed_receive"))
                    self.assertTrue(hasattr(spot, "on_dispatch_event"))

            with zlink.SpotNode(ctx) as server_node:
                with zlink.SpotNode(ctx) as client_node:
                    with zlink.Spot(server_node) as server_spot:
                        with zlink.Spot(client_node) as client_spot:
                            endpoint = _tcp_endpoint()
                            server_node.bind(endpoint)
                            client_node.connect_peer(endpoint)
                            client_spot.set_subscription(b"room:lobby")
                            server_spot.publish(b"room:lobby", [b"hello"])
                            with client_spot.subscribe(flags=zlink.RecvFlags.NONE) as received:
                                self.assertEqual(received.topic, b"room:lobby")
                                self.assertEqual(received.to_bytes_list(), [b"hello"])

    def test_c_string_inputs_reject_embedded_nul(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

        with ctx:
            with zlink.PubSocket(ctx) as publisher:
                with self.assertRaises(ValueError):
                    publisher.publish(b"bad\0topic", b"payload")

            with zlink.SubSocket(ctx) as subscriber:
                with self.assertRaises(ValueError):
                    subscriber.set_subscription(b"bad\0topic")

            with zlink.SpotNode(ctx) as node:
                with zlink.Spot(node) as spot:
                    with self.assertRaises(ValueError):
                        spot.publish(b"bad\0topic", b"payload")

            with zlink.PairSocket(ctx) as pair:
                with self.assertRaises(ValueError):
                    pair.bind("tcp://127.0.0.1\0:5555")

    def test_utils_exports_and_minimal_behavior(self):
        self.assertIsInstance(zlink.version(), tuple)
        self.assertEqual(len(zlink.version()), 3)
        self.assertIsInstance(zlink.errno(), int)
        self.assertIsInstance(zlink.strerror(zlink.errno()), str)
        self.assertIsInstance(zlink.has("tls"), bool)

        with zlink.AtomicCounter() as counter:
            counter.set(3)
            self.assertEqual(counter.value, 3)
            self.assertEqual(counter.increment(), 3)
            self.assertEqual(counter.decrement(), 1)

        with zlink.Stopwatch() as watch:
            self.assertGreaterEqual(watch.intermediate(), 0)
            self.assertGreaterEqual(watch.stop(), 0)

        done = threading.Event()

        def _target():
            done.set()

        thread = zlink.Thread(_target)
        thread.join()
        self.assertTrue(done.is_set())

        with zlink.Timer() as timer:
            self.assertIsNotNone(timer)


if __name__ == "__main__":
    unittest.main()
