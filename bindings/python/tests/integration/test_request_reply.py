import asyncio
import unittest

import zlink

from .helpers import endpoint_for, transports, try_transport, wait_for_socket_event


class RequestReplyScenarioTest(unittest.TestCase):
    def test_request_dealer_router_roundtrip(self):
        ctx = zlink.Context()
        for name, endpoint in transports("request-dealer-router"):
            def run():
                router_socket = zlink.RouterSocket(ctx)
                dealer_socket = zlink.DealerSocket(ctx)
                router = zlink.RequestRouter(router_socket)
                dealer = zlink.RequestDealer(dealer_socket)
                ep = endpoint_for(name, endpoint, "-request")
                router_socket.bind(ep)
                dealer_socket.connect(ep)
                self.assertTrue(
                    wait_for_socket_event(dealer_socket, zlink.PollEvent.POLLOUT, 2000)
                )

                async def scenario():
                    handled = asyncio.Event()

                    async def handler(routing_id, correlation_id, received):
                        self.assertEqual(received.to_bytes_list(), [b"ping"])
                        await router.reply(routing_id, correlation_id, [b"pong"])
                        handled.set()

                    router.on_request(handler)
                    reply = await dealer.request([b"ping"], timeout=2.0)
                    try:
                        self.assertEqual(reply.to_bytes_list(), [b"pong"])
                    finally:
                        reply.close()
                    await asyncio.wait_for(handled.wait(), 2.0)

                asyncio.run(scenario())
                router.close()
                dealer.close()

            try_transport(name, run)
        ctx.close()

    def test_request_router_preserves_data_recv_surface(self):
        ctx = zlink.Context()
        for name, endpoint in transports("request-router-data"):
            def run():
                router_socket = zlink.RouterSocket(ctx)
                dealer_socket = zlink.DealerSocket(ctx)
                router = zlink.RequestRouter(router_socket)
                ep = endpoint_for(name, endpoint, "-data")
                router_socket.bind(ep)
                dealer_socket.connect(ep)
                self.assertTrue(
                    wait_for_socket_event(dealer_socket, zlink.PollEvent.POLLOUT, 2000)
                )

                dealer_socket.send([b"plain-data"])
                received = router.recv()
                try:
                    self.assertEqual(received.to_bytes_list(), [b"plain-data"])
                finally:
                    received.close()
                router.close()
                dealer_socket.close()

            try_transport(name, run)
        ctx.close()
