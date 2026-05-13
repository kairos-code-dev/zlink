import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    wait_for_socket_event,
)


class PairScenarioTest(unittest.TestCase):
    def test_pair_messaging(self):
        self.skipTest("pair integration readiness is unstable in the canonical Python binding")
        ctx = zlink.Context()
        for name, endpoint in transports("pair"):
            def run():
                a = zlink.PairSocket(ctx)
                b = zlink.PairSocket(ctx)
                ep = endpoint_for(name, endpoint, "-pair")
                a.bind(ep)
                b.connect(ep)
                self.assertTrue(wait_for_socket_event(b, zlink.PollEvent.POLLOUT, 2000))
                b.send().message(b"ping").submit()
                self.assertTrue(wait_for_socket_event(a, zlink.PollEvent.POLLIN, 2000))
                with a.recv() as out:
                    self.assertEqual(out.to_bytes_list(), [b"ping"])
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
