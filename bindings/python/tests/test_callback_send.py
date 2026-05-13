# SPDX-License-Identifier: MPL-2.0

"""Regression tests for blocking sends inside receive callbacks."""

import errno
import socket
import struct
import threading
import time
import unittest

import zlink


def _inproc_endpoint(name):
    return f"inproc://callback-send-{name}"


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port, f"tcp://127.0.0.1:{port}"


CHANNEL_NAME = "spot-svc"
TOPIC = b"spot:callback"


class CallbackSendTests(unittest.TestCase):
    def setUp(self):
        try:
            self.ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")

    def tearDown(self):
        if hasattr(self, "ctx") and self.ctx is not None:
            self.ctx.close()

    def test_stream_send_inside_on_packet_raises_explicit_error(self):
        port, endpoint = _tcp_endpoint()
        stream = zlink.StreamSocket(self.ctx)

        done = threading.Event()
        callback_error = []

        def on_message(routing_id, header, body):
            try:
                self.assertIsNotNone(routing_id)
                self.assertEqual(header.to_bytes(), b"")
                self.assertEqual(body.to_bytes(), b"ping")
                stream.send(routing_id).message(b"pong").submit()
            except Exception as exc:
                callback_error.append(exc)
            finally:
                done.set()

        stream.bind(endpoint)
        stream.on_packet(on_message)
        time.sleep(0.05)

        with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
            client.sendall(struct.pack("!HI", 0, len(b"ping")) + b"ping")
            self.assertTrue(done.wait(3.0), "callback timed out")

        self.assertEqual(len(callback_error), 1, f"callback raised: {callback_error}")
        self.assertIsInstance(callback_error[0], zlink.ZlinkError)
        self.assertEqual(callback_error[0].internal_errno, errno.EDEADLK)

        stream.close()

    def test_spot_send_inside_on_dispatch_event_raises_explicit_error(self):
        self.skipTest("spot local publish callback triggering is not guaranteed in the canonical Python binding")
        node = zlink.SpotNode(self.ctx)
        spot = node.create_spot()
        done = threading.Event()
        callback_error = []
        try:
            def on_message(_spot, _event):
                try:
                    spot.send_channel(CHANNEL_NAME).message(b"reply").submit()
                except Exception as exc:
                    callback_error.append(exc)
                finally:
                    done.set()

            spot.on_dispatch_event(on_message)
            spot.set_subscription(TOPIC)
            deadline = time.monotonic() + 3.0
            while not done.is_set():
                if time.monotonic() >= deadline:
                    self.fail("callback timed out")
                spot.publish(TOPIC).message(b"ping").submit()
                done.wait(0.05)
            self.assertEqual(len(callback_error), 1, f"callback raised: {callback_error}")
            self.assertIsInstance(callback_error[0], zlink.ZlinkError)
            self.assertEqual(callback_error[0].internal_errno, errno.EDEADLK)
        finally:
            spot.close()
            node.close()


if __name__ == "__main__":
    unittest.main()
