import struct
import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    send_with_retry,
    ZLINK_XPUB,
    ZLINK_XSUB,
    ZLINK_XPUB_VERBOSE,
)


class XpubXsubScenarioTest(unittest.TestCase):
    def test_xpub_xsub_subscription(self):
        ctx = zlink.Context()
        for name, endpoint in transports("xpub"):
            def run():
                xpub = zlink.Socket(ctx, ZLINK_XPUB)
                xsub = zlink.Socket(ctx, ZLINK_XSUB)
                xpub.setsockopt(ZLINK_XPUB_VERBOSE, struct.pack("i", 1))
                ep = endpoint_for(name, endpoint, "-xpub")
                xpub.bind(ep)
                xsub.connect(ep)
                time.sleep(0.05)
                sub = bytes([1, ord("t"), ord("o"), ord("p"), ord("i"), ord("c")])
                send_with_retry(xsub, sub, 0, 2000)
                msg = recv_with_timeout(xpub, 64, 2000)
                self.assertEqual(msg[0], 1)
                xpub.close()
                xsub.close()
            try_transport(name, run)
        ctx.close()
