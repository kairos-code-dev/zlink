import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    send_with_retry,
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
                time.sleep(0.05)
                send_with_retry(b, b"ping", 0, 2000)
                out = recv_with_timeout(a, 16, 2000)
                self.assertEqual(out[:4], b"ping")
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
