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

    def test_legacy_router_channel_peer_returns_migration_error(self):
        router_endpoint = _tcp_endpoint()
        node = zlink.create_spot_node(self.ctx)

        try:
            with self.assertRaises(zlink.ConnectError) as raised:
                node.connect_router_channel_peer("api", router_endpoint)
            self.assertEqual(raised.exception.result, zlink.ConnectResult.NOT_SUPPORTED)
        finally:
            node.close()


if __name__ == "__main__":
    unittest.main()
