import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
)


class DealerRouterScenarioTest(unittest.TestCase):
    def test_dealer_router_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("dealer-router"):
            if name != "inproc":
                continue

            def run():
                router = zlink.RouterSocket(ctx)
                dealer = zlink.DealerSocket(ctx)
                ep = endpoint_for(name, endpoint, "-dr")
                router.bind(ep)
                dealer.connect(ep)
                time.sleep(0.05)
                dealer.send(b"hello")
                with router.recv() as received:
                    self.assertEqual(received.to_bytes_list(), [b"hello"])
                    self.assertIsNotNone(received.routing_id)
                    router.send(received.routing_id, b"world")
                with dealer.recv() as response:
                    self.assertEqual(response.to_bytes_list(), [b"world"])
                dealer.close()
            try_transport(name, run)
        ctx.close()
