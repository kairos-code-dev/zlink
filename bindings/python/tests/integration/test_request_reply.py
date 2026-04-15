import asyncio
import time
import threading
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
                ep = endpoint_for(name, endpoint, "-request")
                router_socket.bind(ep)
                dealer_socket.connect(ep)
                time.sleep(0.05)

                async def scenario():
                    handled = asyncio.Event()

                    def responder():
                        with router_socket.recv() as received:
                            self.assertIsNotNone(received.request_seq)
                            router_socket.reply(
                                received.routing_id,
                                received.request_seq,
                                [b"pong"],
                            )
                            handled.set()

                    threading.Thread(target=responder, daemon=True).start()
                    reply = await dealer_socket.request([b"ping"], timeout=2.0)
                    try:
                        self.assertEqual([part.to_bytes() for part in reply], [b"pong"])
                    finally:
                        for part in reply:
                            part.close()
                    await asyncio.wait_for(handled.wait(), 2.0)

                asyncio.run(scenario())

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
                ep = endpoint_for(name, endpoint, "-data")
                router_socket.bind(ep)
                dealer_socket.connect(ep)
                time.sleep(0.05)

                dealer_socket.send([b"plain-data"])
                received = router_socket.recv()
                try:
                    self.assertEqual(received.to_bytes_list(), [b"plain-data"])
                finally:
                    received.close()

            try_transport(name, run)
        ctx.close()
