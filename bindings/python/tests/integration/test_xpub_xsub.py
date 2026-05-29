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
                xpub.pub_options.verbose = True
                ep = endpoint_for(name, endpoint, "-xpub")
                xpub.bind(ep)
                xsub.connect(ep)
                xsub.set_subscription(b"topic")
                event = zlink.SubscriptionEvent()
                self.assertTrue(xpub.receive_subscription_event_into(event))
                self.assertTrue(event.subscribed)
                self.assertEqual(event.topic, "topic")
                xpub.close()
                xsub.close()
            try_transport(name, run)
        ctx.close()
