import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
)


class PairScenarioTest(unittest.TestCase):
    def test_pair_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("pair"):
            def run():
                a = zlink.PairSocket(ctx)
                b = zlink.PairSocket(ctx)
                ep = endpoint_for(name, endpoint, "-pair")
                a.bind(ep)
                b.connect(ep)
                time.sleep(0.05)
                b.send(b"ping")
                with a.recv() as out:
                    self.assertEqual(out.to_bytes_list(), [b"ping"])
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
