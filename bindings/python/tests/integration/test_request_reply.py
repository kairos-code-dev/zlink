import asyncio
import time
import unittest

import zlink

from .helpers import endpoint_for, transports, try_transport


class RequestReplyScenarioTest(unittest.TestCase):
    def test_request_dealer_router_roundtrip(self):
        ctx = zlink.Context()
        for name, endpoint in transports("request-dealer-router"):
            if name != "inproc":
                continue

            def run():
                router_socket = zlink.RouterSocket(ctx)
                dealer_socket = zlink.DealerSocket(ctx)
                router = zlink.RequestRouter(router_socket)
                dealer = zlink.RequestDealer(dealer_socket)
                ep = endpoint_for(name, endpoint, "-request")
                router_socket.bind(ep)
                dealer_socket.connect(ep)
                time.sleep(0.05)

                async def scenario():
                    handled = asyncio.Event()

                    def on_receive(received):
                        self.assertIsNotNone(received.request_seq)
                        router.reply(received.routing_id, received.request_seq, [b"pong"])
                        handled.set()

                    router.on_receive(on_receive)
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
            if name != "inproc":
                continue

            def run():
                router_socket = zlink.RouterSocket(ctx)
                dealer_socket = zlink.DealerSocket(ctx)
                router = zlink.RequestRouter(router_socket)
                ep = endpoint_for(name, endpoint, "-data")
                router_socket.bind(ep)
                dealer_socket.connect(ep)
                time.sleep(0.05)

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
