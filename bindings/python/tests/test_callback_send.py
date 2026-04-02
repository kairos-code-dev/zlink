# SPDX-License-Identifier: MPL-2.0

"""Regression tests for send operations inside receive callbacks.

The core C API (zlink_send_rid) is thread-safe and supports being called
from within a zlink_recv_handler callback.  Before the fix in
_socket_base.py, blocking sends from callbacks could deadlock the socket
I/O thread because the retry loop waited for I/O events that could only
be delivered by the same thread.

The fix forces DONTWAIT on sends issued from callback context so the I/O
thread is never blocked in a retry loop.
"""

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
    """Test that routed send works correctly inside an on_receive callback."""

    def setUp(self):
        try:
            self.ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

    def tearDown(self):
        if hasattr(self, "ctx") and self.ctx is not None:
            self.ctx.close()

    def test_router_send_inside_on_receive(self):
        """router.send(payload, routing_id=rid) inside on_receive must not hang."""
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

        self.assertTrue(done.wait(3.0), "callback timed out -- routed send hung")
        self.assertEqual(callback_error, [], f"callback raised: {callback_error}")

        reply = dealer.try_recv()
        if reply is None:
            time.sleep(0.2)
            reply = dealer.try_recv()
        self.assertIsNotNone(reply, "dealer did not receive reply")
        self.assertEqual(reply.to_bytes_list(), [b"pong"])
        reply.close()

        dealer.close()
        router.close()

    def test_dealer_send_inside_on_receive(self):
        """dealer.send(payload) inside on_receive must not hang."""
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

        self.assertTrue(done.wait(3.0), "callback timed out -- send hung")
        self.assertEqual(callback_error, [], f"callback raised: {callback_error}")

        # Router should receive the ack
        ack = router.recv()
        self.assertEqual(ack.to_bytes_list(), [b"ack"])
        ack.close()

        dealer.close()
        router.close()

    def test_pair_send_inside_on_receive(self):
        """pair.send(payload) inside on_receive must not hang."""
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

        self.assertTrue(done.wait(3.0), "callback timed out -- send hung")
        self.assertEqual(callback_error, [], f"callback raised: {callback_error}")

        reply = sender.try_recv()
        if reply is None:
            time.sleep(0.2)
            reply = sender.try_recv()
        self.assertIsNotNone(reply, "sender did not receive reply")
        self.assertEqual(reply.to_bytes_list(), [b"pong"])
        reply.close()

        sender.close()
        receiver.close()

    def test_callback_send_with_backpressure_does_not_hang(self):
        """When pipe is full, send in callback raises instead of deadlocking.

        With router mandatory mode + small HWM, sends that cannot complete
        immediately must raise an error rather than blocking the I/O thread
        forever.
        """
        endpoint = _tcp_endpoint()
        router = zlink.RouterSocket(self.ctx)
        dealer = zlink.DealerSocket(self.ctx)

        router.router_options.mandatory = True
        router.options.send_high_water_mark = 1

        done = threading.Event()
        eagain_raised = threading.Event()

        def on_request(received):
            rid = received.routing_id
            try:
                # First send should succeed; subsequent sends may hit HWM.
                for i in range(20):
                    router.send(f"msg-{i}".encode(), routing_id=rid)
                done.set()
            except zlink.ZlinkError:
                # EAGAIN is expected when the pipe is full -- this is the
                # correct behaviour instead of hanging.
                eagain_raised.set()

        router.bind(endpoint)
        dealer.connect(endpoint)
        time.sleep(0.05)
        router.on_receive(on_request)
        dealer.send(b"ping")

        # Either all sends succeed or we get EAGAIN -- both are acceptable.
        # The critical thing is that we do NOT time out (which would mean hang).
        completed = done.wait(3.0) or eagain_raised.wait(0.5)
        self.assertTrue(
            completed,
            "callback timed out -- blocking send from I/O thread hung",
        )

        dealer.close()
        router.close()


if __name__ == "__main__":
    unittest.main()
