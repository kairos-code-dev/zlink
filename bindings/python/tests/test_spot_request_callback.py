# SPDX-License-Identifier: MPL-2.0

import queue
import socket
import threading
import time
import unittest

import zlink


_ALLOCATED_TCP_PORTS = set()


def _tcp_endpoint():
    while True:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()
        if port not in _ALLOCATED_TCP_PORTS:
            _ALLOCATED_TCP_PORTS.add(port)
            return f"tcp://127.0.0.1:{port}"


def _wait_spot_peer_connected(node, timeout_s=5.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if node.status().connected_peer_count > 0 and node.peers():
            return
        time.sleep(0.01)
    raise TimeoutError("spot peer did not connect within 5s")


class SpotRequestCallbackTests(unittest.TestCase):
    def setUp(self):
        self.ctx = zlink.create_context()

    def tearDown(self):
        if hasattr(self, "ctx") and self.ctx is not None:
            self.ctx.close()

    def test_request_to_spot_callback_completes_via_dispatch_receive(self):
        pub_endpoint = _tcp_endpoint()
        router_endpoint = _tcp_endpoint()
        requester_node = zlink.create_spot_node(self.ctx)
        responder_node = zlink.create_spot_node(self.ctx)
        requester = requester_node.create_spot()
        responder = responder_node.create_spot()
        handled = threading.Event()

        try:
            def on_dispatch_event(spot, info):
                if info.event != zlink.SpotDispatchEvent.ROUTED_READABLE:
                    return
                received = spot.recv_routed(flags=zlink.RecvFlags.DONT_WAIT)
                if received is None:
                    return
                with received:
                    self.assertEqual(received.to_bytes_list(), [b"spot-ping"])
                    self.assertIsNotNone(received.routing_id)
                    self.assertIsNotNone(received.spot_rid)
                    self.assertGreater(received.request_seq, 0)
                    received.reply().message(b"spot-pong").submit()
                handled.set()

            responder.on_dispatch_event(on_dispatch_event)
            responder_node.set_router_bind(router_endpoint)
            responder_node.set_pub_bind(pub_endpoint)
            requester_node.connect_peer(pub_endpoint)
            requester_node.connect_router_channel_peer("api", router_endpoint)
            _wait_spot_peer_connected(requester_node)

            reply_queue = queue.Queue()
            requester.request_to_spot(
                responder_node.routing_id,
                responder.routing_id,
            ).message(b"spot-ping").timeout(2.0).submit(
                lambda result, parts: reply_queue.put((result, parts))
            )
            result, reply = reply_queue.get(timeout=2.0)
            self.assertEqual(result, zlink.RequestResult.OK)
            try:
                self.assertEqual([part.to_bytes() for part in reply], [b"spot-pong"])
            finally:
                for part in reply:
                    part.close()
            self.assertTrue(handled.wait(2.0), "routed dispatch receive did not fire")
        finally:
            responder.close()
            requester.close()
            responder_node.close()
            requester_node.close()


if __name__ == "__main__":
    unittest.main()
