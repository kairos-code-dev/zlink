import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    send_with_retry,
    ZLINK_PUB,
    ZLINK_SUB,
    ZLINK_SUBSCRIBE,
)


class PubSubScenarioTest(unittest.TestCase):
    def test_pubsub_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("pubsub"):
            def run():
                pub = zlink.Socket(ctx, ZLINK_PUB)
                sub = zlink.Socket(ctx, ZLINK_SUB)
                ep = endpoint_for(name, endpoint, "-pubsub")
                pub.bind(ep)
                sub.connect(ep)
                sub.setsockopt(ZLINK_SUBSCRIBE, b"topic")
                time.sleep(0.05)
                send_with_retry(pub, b"topic payload", 0, 2000)
                buf = recv_with_timeout(sub, 64, 2000)
                self.assertTrue(buf.startswith(b"topic"))
                pub.close()
                sub.close()
            try_transport(name, run)
        ctx.close()
