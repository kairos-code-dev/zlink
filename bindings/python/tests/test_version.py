import unittest

import time

import zlink


class VersionTests(unittest.TestCase):
    def test_version_matches_core(self):
        try:
            major, minor, patch = zlink.version()
        except OSError:
            self.skipTest("zlink native library not found")
        self.assertEqual(major, 0)
        self.assertEqual(minor, 7)
        self.assertEqual(patch, 0)

    def test_pair_send_recv(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")
        s1 = zlink.Socket(ctx, zlink.SocketType.PAIR)
        s2 = zlink.Socket(ctx, zlink.SocketType.PAIR)
        endpoint = b"inproc://py-pair"
        s1.bind(endpoint.decode())
        s2.connect(endpoint.decode())
        payload = b"ping"
        for _ in range(50):
            try:
                s1.send(payload)
                break
            except zlink.ZlinkError as exc:
                if exc.errno != 11:
                    raise
                time.sleep(0.01)
        data = s2.recv(16)
        self.assertEqual(data, payload)
        s1.close()
        s2.close()
        ctx.close()

if __name__ == "__main__":
    unittest.main()
