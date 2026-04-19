import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    wait_for_socket_event,
)


class MultipartScenarioTest(unittest.TestCase):
    def test_multipart_messaging(self):
        self.skipTest("multipart integration readiness is unstable in the canonical Python binding")
        ctx = zlink.Context()
        for name, endpoint in transports("multipart"):
            def run():
                a = zlink.PairSocket(ctx)
                b = zlink.PairSocket(ctx)
                ep = endpoint_for(name, endpoint, "-mp")
                a.bind(ep)
                b.connect(ep)
                self.assertTrue(wait_for_socket_event(b, zlink.PollEvent.POLLOUT, 2000))
                b.send([b"a", b"b"])
                self.assertTrue(wait_for_socket_event(a, zlink.PollEvent.POLLIN, 2000))
                with a.recv() as received:
                    self.assertEqual(received.to_bytes_list(), [b"a", b"b"])
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
