import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    wait_for_socket_event,
    ZLINK_PAIR,
)


class MultipartScenarioTest(unittest.TestCase):
    def test_multipart_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("multipart"):
            def run():
                a = zlink.Socket(ctx, ZLINK_PAIR)
                b = zlink.Socket(ctx, ZLINK_PAIR)
                ep = endpoint_for(name, endpoint, "-mp")
                a.bind(ep)
                b.connect(ep)
                self.assertTrue(
                    wait_for_socket_event(b, zlink.PollEvent.POLLOUT, 2000)
                )
                b.send_multipart([b"a", b"b"])
                self.assertTrue(
                    wait_for_socket_event(a, zlink.PollEvent.POLLIN, 2000)
                )
                with a.recv_multipart() as received:
                    self.assertEqual(received.to_bytes_list(), [b"a", b"b"])
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
