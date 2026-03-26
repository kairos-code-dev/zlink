import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    wait_for_socket_event,
    ZLINK_PAIR,
)


class PairScenarioTest(unittest.TestCase):
    def test_pair_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("pair"):
            def run():
                a = zlink.Socket(ctx, ZLINK_PAIR)
                b = zlink.Socket(ctx, ZLINK_PAIR)
                ep = endpoint_for(name, endpoint, "-pair")
                a.bind(ep)
                b.connect(ep)
                self.assertTrue(
                    wait_for_socket_event(b, zlink.PollEvent.POLLOUT, 2000)
                )
                b.send(b"ping")
                self.assertTrue(
                    wait_for_socket_event(a, zlink.PollEvent.POLLIN, 2000)
                )
                with a.recv_message() as out:
                    self.assertEqual(out.to_bytes(), b"ping")
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
