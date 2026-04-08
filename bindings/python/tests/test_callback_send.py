# SPDX-License-Identifier: MPL-2.0

"""Regression tests for blocking sends inside receive callbacks.

Callback context must not silently change blocking send semantics.
Blocking send/publish APIs invoked from a receive callback now fail with
an explicit error instead of being downgraded to non-blocking behavior.
"""

import errno
import socket
import threading
import time
import unittest

import zlink


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


class CallbackSendTests(unittest.TestCase):
    """Test callback send/publish behavior is explicit and non-silent."""

    def setUp(self):
        try:
            self.ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

    def tearDown(self):
        if hasattr(self, "ctx") and self.ctx is not None:
            self.ctx.close()

    def test_router_send_inside_on_receive_raises_explicit_error(self):
        endpoint = _tcp_endpoint()
        router = zlink.RouterSocket(self.ctx)
        dealer = zlink.DealerSocket(self.ctx)

        done = threading.Event()
        callback_error = []

        def on_request(received):
            try:
                rid = received.routing_id
                self.assertIsNotNone(rid)
                router.send(b"pong", routing_id=rid)
                done.set()
            except Exception as exc:
                callback_error.append(exc)
                done.set()

        router.bind(endpoint)
        dealer.connect(endpoint)
        time.sleep(0.05)
        router.on_receive(on_request)
        dealer.send(b"ping")

        self.assertTrue(done.wait(3.0), "callback timed out")
        self.assertEqual(len(callback_error), 1, f"callback raised: {callback_error}")
        self.assertIsInstance(callback_error[0], zlink.ZlinkError)
        self.assertEqual(callback_error[0].errno, errno.EDEADLK)

        dealer.close()
        router.close()

    def test_dealer_send_inside_on_receive_raises_explicit_error(self):
        endpoint = _tcp_endpoint()
        router = zlink.RouterSocket(self.ctx)
        dealer = zlink.DealerSocket(self.ctx)

        done = threading.Event()
        callback_error = []

        def on_reply(received):
            try:
                self.assertEqual(received.to_bytes_list(), [b"pong"])
                dealer.send(b"ack")
                done.set()
            except Exception as exc:
                callback_error.append(exc)
                done.set()

        router.bind(endpoint)
        dealer.connect(endpoint)
        time.sleep(0.05)

        dealer.on_receive(on_reply)
        dealer.send(b"ping")

        # Router receives the ping and sends pong
        request = router.recv()
        rid = request.routing_id
        self.assertIsNotNone(rid)
        router.send(b"pong", routing_id=rid)
        request.close()

        self.assertTrue(done.wait(3.0), "callback timed out")
        self.assertEqual(len(callback_error), 1, f"callback raised: {callback_error}")
        self.assertIsInstance(callback_error[0], zlink.ZlinkError)
        self.assertEqual(callback_error[0].errno, errno.EDEADLK)

        dealer.close()
        router.close()

    def test_pair_send_inside_on_receive_raises_explicit_error(self):
        endpoint = _tcp_endpoint()
        sender = zlink.PairSocket(self.ctx)
        receiver = zlink.PairSocket(self.ctx)

        done = threading.Event()
        callback_error = []

        def on_message(received):
            try:
                self.assertEqual(received.to_bytes_list(), [b"ping"])
                receiver.send(b"pong")
                done.set()
            except Exception as exc:
                callback_error.append(exc)
                done.set()

        sender.bind(endpoint)
        receiver.connect(endpoint)
        time.sleep(0.05)
        receiver.on_receive(on_message)
        sender.send(b"ping")

        self.assertTrue(done.wait(3.0), "callback timed out")
        self.assertEqual(len(callback_error), 1, f"callback raised: {callback_error}")
        self.assertIsInstance(callback_error[0], zlink.ZlinkError)
        self.assertEqual(callback_error[0].errno, errno.EDEADLK)

        sender.close()
        receiver.close()

    def test_spot_publish_inside_on_subscribe_raises_explicit_error(self):
        endpoint = _tcp_endpoint()
        server_node = zlink.SpotNode(self.ctx)
        client_node = zlink.SpotNode(self.ctx)
        server_spot = zlink.Spot(server_node)
        client_spot = zlink.Spot(client_node)
        done = threading.Event()
        callback_error = []

        def on_message(received):
            try:
                client_spot.publish(b"room:lobby", [b"reply"])
            except Exception as exc:
                callback_error.append(exc)
            finally:
                done.set()

        server_node.bind(endpoint)
        client_node.connect_peer(endpoint)
        client_spot.set_subscription(b"room:lobby")
        client_spot.on_subscribe(on_message)
        time.sleep(0.05)
        server_spot.publish(b"room:lobby", [b"hello"])

        self.assertTrue(done.wait(3.0), "callback timed out")
        self.assertEqual(len(callback_error), 1, f"callback raised: {callback_error}")
        self.assertIsInstance(callback_error[0], zlink.ZlinkError)
        self.assertEqual(callback_error[0].errno, errno.EDEADLK)

        client_spot.close()
        server_spot.close()
        client_node.close()
        server_node.close()


if __name__ == "__main__":
    unittest.main()
