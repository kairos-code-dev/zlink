import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
)

ZLINK_PUB_OPT_VERBOSE = 0x3301


class XpubXsubScenarioTest(unittest.TestCase):
    def test_xpub_xsub_subscription(self):
        ctx = zlink.Context()
        for name, endpoint in transports("xpub"):
            def run():
                xpub = zlink.XPubSocket(ctx)
                xsub = zlink.XSubSocket(ctx)
                xpub.set_pub_option(ZLINK_PUB_OPT_VERBOSE, (1).to_bytes(4, "little"))
                ep = endpoint_for(name, endpoint, "-xpub")
                xpub.bind(ep)
                xsub.connect(ep)
                xsub.subscribe(b"topic")
                event = xpub.subscription_event()
                self.assertTrue(event["subscribed"])
                self.assertEqual(event["topic"], b"topic")
                xpub.close()
                xsub.close()
            try_transport(name, run)
        ctx.close()
