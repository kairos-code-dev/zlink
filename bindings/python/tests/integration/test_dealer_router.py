import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    send_with_retry,
    ZLINK_DEALER,
    ZLINK_ROUTER,
    ZLINK_SNDMORE,
)


class DealerRouterScenarioTest(unittest.TestCase):
    def test_dealer_router_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("dealer-router"):
            def run():
                router = zlink.Socket(ctx, ZLINK_ROUTER)
                dealer = zlink.Socket(ctx, ZLINK_DEALER)
                ep = endpoint_for(name, endpoint, "-dr")
                router.bind(ep)
                dealer.connect(ep)
                time.sleep(0.05)
                send_with_retry(dealer, b"hello", 0, 2000)
                rid = recv_with_timeout(router, 256, 2000)
                payload = recv_with_timeout(router, 256, 2000)
                self.assertEqual(payload.strip(b"\0"), b"hello")
                send_with_retry(router, rid, ZLINK_SNDMORE, 2000)
                send_with_retry(router, b"world", 0, 2000)
                resp = recv_with_timeout(dealer, 64, 2000)
                self.assertEqual(resp.strip(b"\0"), b"world")
                router.close()
                dealer.close()
            try_transport(name, run)
        ctx.close()
