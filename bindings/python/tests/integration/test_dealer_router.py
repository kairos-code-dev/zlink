import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    wait_for_socket_event,
    ZLINK_DEALER,
    ZLINK_ROUTER,
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
                self.assertTrue(
                    wait_for_socket_event(dealer, zlink.PollEvent.POLLOUT, 2000)
                )
                dealer.send(b"hello")
                self.assertTrue(
                    wait_for_socket_event(router, zlink.PollEvent.POLLIN, 2000)
                )
                with router.recv_message() as received:
                    self.assertEqual(received.to_bytes(), b"hello")
                    self.assertIsNotNone(received.routing_id)
                    router.send(received.routing_id, zlink.SendFlag.SNDMORE)
                router.send(b"world")
                self.assertTrue(
                    wait_for_socket_event(dealer, zlink.PollEvent.POLLIN, 2000)
                )
                with dealer.recv_message() as response:
                    self.assertEqual(response.to_bytes(), b"world")
                router.close()
                dealer.close()
            try_transport(name, run)
        ctx.close()
