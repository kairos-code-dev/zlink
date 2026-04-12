import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
)


class MultipartScenarioTest(unittest.TestCase):
    def test_multipart_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("multipart"):
            def run():
                a = zlink.PairSocket(ctx)
                b = zlink.PairSocket(ctx)
                ep = endpoint_for(name, endpoint, "-mp")
                a.bind(ep)
                b.connect(ep)
                time.sleep(0.05)
                b.send([b"a", b"b"])
                with a.recv() as received:
                    self.assertEqual(received.to_bytes_list(), [b"a", b"b"])
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
