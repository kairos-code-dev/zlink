import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
)

class XpubXsubScenarioTest(unittest.TestCase):
    def test_xpub_xsub_subscription(self):
        ctx = zlink.Context()
        for name, endpoint in transports("xpub"):
            def run():
                xpub = zlink.XPubSocket(ctx)
                xsub = zlink.XSubSocket(ctx)
                xpub.publisher_options.verbose = True
                ep = endpoint_for(name, endpoint, "-xpub")
                xpub.bind(ep)
                xsub.connect(ep)
                xsub.set_subscription(b"topic")
                event = xpub.receive_subscription_event()
                self.assertTrue(event.subscribed)
                self.assertEqual(event.topic, b"topic")
                xpub.close()
                xsub.close()
            try_transport(name, run)
        ctx.close()
