import unittest

import zlink


class VersionTests(unittest.TestCase):
    def test_version_matches_core(self):
        try:
            major, minor, patch = zlink.version()
        except OSError:
            self.skipTest("zlink native library not found")
        self.assertEqual(major, 5)
        self.assertEqual(minor, 0)
        self.assertEqual(patch, 30)

    def test_pair_send_recv(self):
        try:
            ctx = zlink.Context()
        except OSError:
            self.skipTest("zlink native library not found")
        with ctx:
            with zlink.PairSocket(ctx) as s1:
                with zlink.PairSocket(ctx) as s2:
                    endpoint = "inproc://py-pair"
                    s1.bind(endpoint)
                    s2.connect(endpoint)
                    payload = b"ping"
                    s1.send(payload)
                    with s2.recv() as received:
                        self.assertEqual(received.to_bytes_list(), [payload])

if __name__ == "__main__":
    unittest.main()
