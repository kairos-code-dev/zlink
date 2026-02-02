import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    send_with_retry,
    ZLINK_PAIR,
    ZLINK_SNDMORE,
)


class MultipartScenarioTest(unittest.TestCase):
    def test_multipart_messaging(self):
        ctx = zlink.Context()
        for name, endpoint in transports("multipart"):
            def run():
                a = zlink.Socket(ctx, ZLINK_PAIR)
                b = zlink.Socket(ctx, ZLINK_PAIR)
                ep = endpoint_for(name, endpoint, "-mp")
                a.bind(ep)
                b.connect(ep)
                time.sleep(0.05)
                send_with_retry(b, b"a", ZLINK_SNDMORE, 2000)
                send_with_retry(b, b"b", 0, 2000)
                part1 = recv_with_timeout(a, 16, 2000)
                part2 = recv_with_timeout(a, 16, 2000)
                self.assertEqual(part1.strip(b"\0"), b"a")
                self.assertEqual(part2.strip(b"\0"), b"b")
                a.close()
                b.close()
            try_transport(name, run)
        ctx.close()
